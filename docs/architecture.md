# Architecture Overview

This document provides a deep dive into the system architecture of the mem-kv-cpp in-memory key-value store. It explains how data flows from a client command to physical disk, and how the system achieves high performance through careful design decisions.

## The Layered Model

The system is organized into multiple layers, each with a clear responsibility:

### 1. Network Layer (`src/net/`)

**Components:**
- `Server`: Manages the TCP socket lifecycle (bind, listen, accept)
- `Connection`: Handles individual client sessions

**Responsibilities:**
- Accept incoming TCP connections
- Read raw bytes from sockets
- Write responses back to clients
- Manage connection lifecycle

**Key Design Decision:** The `Server` uses a thread pool to distribute client connections, avoiding the overhead of spawning a new thread per connection.

### 2. Protocol Layer (`src/protocol/`)

**Components:**
- `Parser`: Tokenizes incoming byte streams into structured commands
- `Command`: Defines command types (SET, GET, DEL, COMPACT) and parsed command structures

**Responsibilities:**
- Parse both plain-text and RESP (Redis Serialization Protocol) formats
- Validate command syntax
- Transform raw strings into `ParsedCommand` objects

**Key Design Decision:** Protocol parsing is completely decoupled from networking and storage. This allows us to add new protocols (e.g., binary protocols) without touching the storage engine.

### 3. Storage Layer (`src/storage/`)

**Components:**
- `KVStore`: The main storage interface (uses PIMPL pattern)
- `KVStore::Impl`: The actual implementation with sharded data structures

**Responsibilities:**
- Execute commands on the in-memory data structures
- Persist writes to the Write-Ahead Log (WAL)
- Recover state from disk on startup
- Perform log compaction

## Data Flow: From `nc` Command to Disk

Let's trace what happens when a client sends `SET model:user:123 prediction EX 3600` (ML inference cache):

```
1. Client: echo "SET model:user:123 prediction EX 3600" | nc localhost 8080
   ↓
2. Server::run() [Main Thread]
   - accept() blocks until client connects
   - Receives client_socket file descriptor
   - Enqueues socket to ThreadPool
   ↓
3. ThreadPool::worker_loop() [Worker Thread]
   - Wakes up from condition_variable wait
   - Pops client_socket from queue
   - Creates Connection object with WriteBatcher reference
   ↓
4. Connection::handle() [Worker Thread]
   - recv() reads bytes: "SET model:user:123 prediction EX 3600\n"
   - Calls Parser::parse() to tokenize
   ↓
5. Parser::parse() [Worker Thread]
   - Creates ParsedCommand{type=SET, key="model:user:123", value="prediction", ttl_seconds=3600}
   - Returns structured command object
   ↓
6. Connection::handle() [Worker Thread]
   - Detects SET command (write operation)
   - Routes to WriteBatcher::add_to_batch()
   - Immediately sends "OK\n" response (non-blocking)
   ↓
7. WriteBatcher::add_to_batch() [Worker Thread]
   - Adds command to current_batch_
   - If batch size >= 50, triggers flush_to_store()
   ↓
8. WriteBatcher::flush_to_store() [Worker Thread or Background Thread]
   - Records batch statistics (Metrics::record_batch())
   - For each command in batch:
     ↓
9. KVStore::Impl::set() [Worker Thread]
   - Computes shard index: hash("model:user:123") % 16 = shard_idx
   - Acquires lock on shards[shard_idx].mtx
   - Creates CacheEntry with expiry_at_ms = now + 3600000
   - Updates shards[shard_idx].data["model:user:123"] = CacheEntry
   - Releases shard lock
   - Acquires journal_mtx_ lock
   - Writes "SET model:user:123 prediction EX 3600\n" to journal_ stream
   - Releases journal lock
   ↓
10. Background Flusher Thread [Separate Thread]
    - Every 100ms, acquires journal_mtx_ lock
    - Calls journal_.flush() to force OS write
    - Releases lock
    ↓
11. Operating System
    - Buffers the write in page cache
    - Eventually syncs to physical disk
```

## Sharding Strategy

The storage layer uses **sharded locking** to reduce contention:

```cpp
static constexpr size_t NUM_SHARDS = 16;

struct Shard {
    std::unordered_map<std::string, std::string> data;
    std::mutex mtx;
};

Shard shards[NUM_SHARDS];
```

**Hash Function:**
```cpp
size_t get_shard_index(const std::string& key) {
    return std::hash<std::string>{}(key) % NUM_SHARDS;
}
```

**Why This Works:**
- **Parallel Writes:** Two threads writing to different keys can proceed simultaneously if they hash to different shards
- **Reduced Contention:** Instead of one global lock, we have 16 independent locks
- **Load Distribution:** `std::hash` provides uniform distribution across shards

**Example:**
- Thread A writes `SET user_1 value` → hash("user_1") % 16 = 3 → locks shard[3]
- Thread B writes `SET user_2 value` → hash("user_2") % 16 = 7 → locks shard[7]
- **Result:** Both operations proceed in parallel with zero contention

## Thread Lifecycle

### Main Thread (Server::run())

```
while (true) {
    client_socket = accept(server_fd, ...);  // Blocks here
    thread_pool_->enqueue(client_socket);    // Non-blocking
}
```

The main thread's only job is to accept connections and enqueue them. It never blocks on client I/O.

### Worker Threads (ThreadPool)

```
void worker_loop() {
    while (true) {
        condition_variable.wait(...);  // Sleep until task available
        socket = tasks_.front();
        tasks_.pop();
        handler_(socket);  // Process client connection
    }
}
```

**Key Benefits:**
- **No Thread Creation Overhead:** Threads are created once at startup
- **No Context Switching:** Threads sleep on condition_variable, not busy-wait
- **Work Stealing:** Any idle worker can pick up the next task

### Background Flusher Thread

```
void background_flush() {
    while (running_) {
        sleep(100ms);
        if (!is_compacting_) {
            journal_.flush();  // Force OS to write buffered data
        }
    }
}
```

This thread ensures durability by periodically flushing the WAL to disk, trading off a small risk of data loss (last 100ms) for significantly better performance.

## Performance Characteristics

| Operation | Complexity | Lock Contention | ML Optimization |
|-----------|-----------|-----------------|-----------------|
| SET       | O(1) avg  | 1/16 shard lock | Batched (50x reduction) |
| GET       | O(1) avg  | 1/16 shard lock | TTL-aware eviction |
| MGET      | O(k) avg  | k/16 shard locks | Shard-grouped batching |
| DEL       | O(1) avg  | 1/16 shard lock | Batched (50x reduction) |
| COMPACT   | O(n)      | All shard locks (brief) | Skips expired entries |

The sharding strategy ensures that in a high-concurrency scenario, most operations can proceed in parallel without blocking each other. Micro-batching further reduces lock contention by grouping writes together.

## ML-Optimized Operations

### TTL Cache Entry Structure

```cpp
struct CacheEntry {
    std::string value;
    long long expiry_at_ms = 0;  // Unix timestamp in ms
    
    bool is_expired() const {
        if (expiry_at_ms == 0) return false;
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return now_ms > expiry_at_ms;
    }
};
```

**Lazy Eviction:** Expired entries are removed on GET access, avoiding background cleanup overhead.

### Write Batching Flow

```
Client SET → WriteBatcher → Batch Buffer (up to 50 commands)
                              ↓
                         Background Thread (every 10ms)
                              ↓
                         Flush Batch → KVStore (single lock acquisition)
```

**Performance Impact:** 50 writes → 1 lock acquisition = 50x reduction in contention.

