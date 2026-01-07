# Concurrency Model

This document explains the concurrency design decisions that enable the mem-kv-cpp server to handle tens of thousands of requests per second while maintaining data consistency.

## Evolution: From Global Lock to Sharded Locks

### Phase 1: Single Global Mutex

**Initial Implementation:**
```cpp
class KVStore {
    std::unordered_map<std::string, std::string> data_;
    std::mutex mtx_;  // One lock for everything
};
```

**Problem:** Every operation (SET, GET, DEL) must acquire the same lock. Even if two threads are accessing completely different keys, one must wait for the other.

**Performance:** ~2,200 RPS with 10 concurrent clients

### Phase 2: Sharded Mutexes

**Current Implementation:**
```cpp
static constexpr size_t NUM_SHARDS = 16;

struct Shard {
    std::unordered_map<std::string, std::string> data;
    std::mutex mtx;
};

Shard shards[NUM_SHARDS];
```

**Key Insight:** Use `hash(key) % 16` to distribute keys across 16 independent shards. Each shard has its own mutex.

**Performance:** ~96,000 RPS with 100 concurrent clients

**Why 16 Shards?**
- Too few (e.g., 2-4): Still significant contention
- Too many (e.g., 128+): Memory overhead and cache line false sharing
- 16 is a sweet spot: Enough parallelism, minimal overhead

## Lock Granularity

### Fine-Grained Locking: Shard Level

```cpp
std::string get(const std::string& key) {
    size_t idx = get_shard_index(key);  // hash(key) % 16
    std::lock_guard<std::mutex> lock(shards[idx].mtx);
    
    auto it = shards[idx].data.find(key);
    return (it != shards[idx].data.end()) ? it->second : "(nil)";
}
```

**Lock Scope:** Only the specific shard containing the key is locked. Other shards remain accessible to other threads.

**Lock Duration:** Minimal—just long enough to perform the hash map lookup.

### Coarse-Grained Locking: Journal Writes

```cpp
void set(const std::string& key, const std::string& value) {
    // Fine-grained: Lock only the shard
    {
        size_t idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shards[idx].mtx);
        shards[idx].data[key] = value;
    }
    
    // Coarse-grained: Lock the entire journal
    {
        std::lock_guard<std::mutex> lock(journal_mtx_);
        journal_ << "SET " << key << " " << value << "\n";
    }
}
```

**Why Separate Locks?**
- Shard lock: Protects in-memory data (needs fine granularity)
- Journal lock: Protects file I/O (coarse granularity is acceptable)

## The WAL Bottleneck

### The Problem: Synchronous Flushing

**Naive Approach:**
```cpp
void set(const std::string& key, const std::string& value) {
    // ... update in-memory map ...
    journal_ << "SET " << key << " " << value << "\n";
    journal_.flush();  // BLOCKS until OS writes to disk!
}
```

**Performance Impact:**
- `flush()` is a system call that blocks until data is written to disk
- Disk I/O latency: ~1-10ms per operation
- Maximum throughput: ~100-1,000 RPS (limited by disk speed)

### The Solution: Asynchronous Flushing

**Current Approach:**
```cpp
void set(const std::string& key, const std::string& value) {
    // ... update in-memory map ...
    {
        std::lock_guard<std::mutex> lock(journal_mtx_);
        journal_ << "SET " << key << " " << value << "\n";
        // NO flush() here!
    }
}

// Background thread flushes every 100ms
void background_flush() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(journal_mtx_);
        journal_.flush();
    }
}
```

**Performance Impact:**
- Writes are buffered in memory (fast)
- Batch flushing reduces system call overhead
- Maximum throughput: ~96,000 RPS

### The Trade-off: Durability vs. Performance

**Risk:** If the server crashes, up to 100ms of writes may be lost (data in memory buffers that haven't been flushed).

**Mitigation Strategies:**
1. **Acceptable for Many Use Cases:** 100ms of data loss is acceptable for caching, session storage, etc.
2. **Configurable:** Could add a `FLUSH` command for critical writes
3. **Future Enhancement:** Could use `fsync()` for stronger guarantees (with performance cost)

**Industry Standard:** Redis uses the same approach by default (`appendfsync everysec`).

## Race Condition Prevention

### Thread Pool Synchronization

**Challenge:** Multiple worker threads competing for tasks from a shared queue.

**Solution:** `std::condition_variable` + `std::mutex`

```cpp
void worker_loop() {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // Sleep until work is available
        condition_.wait(lock, [this] { 
            return stop_ || !tasks_.empty(); 
        });
        
        if (stop_ && tasks_.empty()) return;
        
        int socket = tasks_.front();
        tasks_.pop();
        lock.unlock();  // Release lock before processing
        
        handler_(socket);  // Process client (may take time)
    }
}

void enqueue(int client_socket) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        tasks_.push(client_socket);
    }
    condition_.notify_one();  // Wake up one waiting worker
}
```

**Key Points:**
- `condition_variable.wait()` atomically releases the lock and sleeps
- `notify_one()` wakes exactly one thread (efficient)
- Lock is held only during queue manipulation, not during client processing

### Compaction Synchronization

**Challenge:** Background flusher thread must not interfere with compaction's file rename operation.

**Solution:** `std::atomic<bool>` flag

```cpp
std::atomic<bool> is_compacting_{false};

void background_flush() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (is_compacting_) continue;  // Skip flushing during compaction
        
        std::lock_guard<std::mutex> lock(journal_mtx_);
        journal_.flush();
    }
}

void compact() {
    is_compacting_ = true;  // Signal to background thread
    
    // ... create snapshot ...
    
    {
        std::lock_guard<std::mutex> lock(journal_mtx_);
        journal_.close();
        std::rename(temp_file, journal_file);  // Atomic swap
        journal_.open(journal_file, std::ios::app);
    }
    
    is_compacting_ = false;  // Resume flushing
}
```

**Why Atomic?**
- `std::atomic<bool>` ensures lock-free reads/writes
- No mutex needed for the flag check (performance critical path)
- Prevents race condition where flusher tries to flush during rename

## Deadlock Prevention

**Potential Deadlock Scenario:**
```
Thread A: Locks shard[0] → tries to lock journal_mtx_
Thread B: Locks journal_mtx_ → tries to lock shard[0]
```

**Prevention Strategy:**
- **Always acquire locks in the same order:** Shard lock first, then journal lock
- **Lock duration:** Release shard lock before acquiring journal lock when possible
- **No circular dependencies:** Journal operations never need shard locks

## Performance Metrics

| Configuration | Lock Contention | Throughput |
|--------------|----------------|------------|
| Global mutex | High (all ops) | ~2,200 RPS |
| 16 shards    | Low (1/16 ops) | ~96,000 RPS |
| 16 shards + batching | Very Low (1/50 ops) | ~162,000 RPS |

The sharded locking strategy reduces lock contention by ~94% (1/16). Adding micro-batching further reduces contention by grouping 50 writes into a single lock acquisition, achieving ~162,000 RPS throughput.

## Write Batching: The ML Secret Sauce

**Problem:** ML feature stores receive millions of writes per second. Each write requires:
- Lock acquisition (mutex contention)
- Disk I/O (WAL append)
- System call overhead

**Solution:** Micro-batching groups writes together:

```cpp
class WriteBatcher {
    static constexpr size_t BATCH_SIZE_THRESHOLD = 50;
    
    void flush_to_store() {
        // Acquire lock once for 50 writes
        // Write to WAL once for 50 writes
        // 50x reduction in overhead
    }
};
```

**Performance Impact:**
- **Lock Contention:** 50 writes → 1 lock acquisition (50x reduction)
- **System Calls:** 50 WAL writes → 1 batch write (50x reduction)
- **Throughput:** 2-5x improvement for write-heavy workloads
- **Latency:** <10ms batching delay (acceptable for feature stores)

