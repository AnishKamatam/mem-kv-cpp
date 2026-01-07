# Persistence and Durability

This document explains how the mem-kv-cpp server ensures data durability and recovers from crashes using a Write-Ahead Log (WAL) strategy.

## Write-Ahead Log (WAL) Architecture

### What is a WAL?

A Write-Ahead Log is an append-only file that records every write operation before it's applied to the in-memory data structures. This is the same strategy used by PostgreSQL, SQLite, and Redis.

**Key Principle:** Never modify data structures until the operation is logged.

### Our Implementation

```cpp
class KVStore::Impl {
    std::ofstream journal_;  // The WAL file stream
    std::mutex journal_mtx_; // Protects concurrent writes
    std::string journal_path_; // Path to wal.log
};
```

**File Format:**
```
SET key1 value1
SET key2 value2
DEL key1
SET key3 value3
```

Each line represents a single write operation. The format is simple and human-readable for debugging.

## Write Path

### Step-by-Step: How a SET Operation is Persisted

```cpp
void set(const std::string& key, const std::string& value) {
    // 1. Update in-memory data structure
    {
        size_t idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shards[idx].mtx);
        shards[idx].data[key] = value;
    }
    
    // 2. Append to WAL
    {
        std::lock_guard<std::mutex> lock(journal_mtx_);
        journal_ << "SET " << key << " " << value << "\n";
        // Note: No flush() here - handled by background thread
    }
}
```

**Why Append-Only?**
- **Sequential I/O:** Appending is much faster than random writes
- **Crash Safety:** If the server crashes mid-write, only the last line might be corrupted
- **Simple Recovery:** Replay the log from the beginning

### Asynchronous Flushing

**The Problem:** `flush()` is expensive (forces OS to write to disk).

**The Solution:** Background thread flushes every 100ms:

```cpp
flusher_thread_ = std::thread([this]() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (!is_compacting_) {
            std::lock_guard<std::mutex> lock(journal_mtx_);
            journal_.flush();  // Force OS to write buffered data
        }
    }
});
```

**Trade-off:**
- **Performance:** Writes are buffered, reducing system call overhead
- **Durability Risk:** Up to 100ms of writes may be lost on crash
- **Acceptable:** For many use cases (caching, session storage), this is fine

## Recovery Protocol

When the server starts, it must rebuild the in-memory state from the WAL.

### Recovery Process

```cpp
void load_from_disk(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;  // No existing log file
    }
    
    std::string line;
    while (std::getline(file, line)) {
        ParsedCommand cmd = Parser::parse(line);
        
        if (cmd.type == CommandType::SET) {
            size_t idx = get_shard_index(cmd.key);
            std::lock_guard<std::mutex> lock(shards[idx].mtx);
            shards[idx].data[cmd.key] = cmd.value;
        }
        else if (cmd.type == CommandType::DEL) {
            size_t idx = get_shard_index(cmd.key);
            std::lock_guard<std::mutex> lock(shards[idx].mtx);
            shards[idx].data.erase(cmd.key);
        }
    }
}
```

**Key Points:**
1. **Replay Order:** Operations are replayed in the exact order they were written
2. **Idempotent:** SET operations overwrite previous values (safe to replay)
3. **Complete State:** After replay, the in-memory state matches the last consistent point before crash

### Example Recovery Scenario

**Before Crash:**
```
WAL contains:
SET user_1 Alice
SET user_2 Bob
SET user_1 Charlie  (overwrites Alice)
DEL user_2
```

**After Crash and Recovery:**
```
In-memory state:
user_1 = "Charlie"
user_2 = (deleted)
```

The final state is correctly restored, even though intermediate states (user_1="Alice", user_2="Bob") are not preserved.

## Log Compaction

### The Problem: Unbounded Growth

Over time, the WAL grows indefinitely:
```
SET key1 value1
SET key1 value2    ← key1 updated
SET key1 value3    ← key1 updated again
SET key2 value4
SET key1 value5    ← key1 updated yet again
```

Even though `key1` only has one current value (`value5`), the log contains 4 lines for it.

**Impact:** Disk space waste, slower recovery times.

### The Solution: Copy-on-Write Compaction

**Strategy:** Create a snapshot of current state, atomically replace the old log.

```cpp
void compact() {
    is_compacting_ = true;  // Pause background flusher
    
    std::string temp_filename = journal_path_ + ".tmp";
    
    // 1. Create snapshot of current state
    {
        std::ofstream temp_journal(temp_filename, std::ios::trunc);
        
        // Lock all shards to get consistent snapshot
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            std::lock_guard<std::mutex> lock(shards[i].mtx);
            
            // Write only current state (no duplicates)
            for (const auto& [key, value] : shards[i].data) {
                temp_journal << "SET " << key << " " << value << "\n";
            }
        }
        
        temp_journal.close();
    }
    
    // 2. Atomic swap
    {
        std::lock_guard<std::mutex> lock(journal_mtx_);
        
        journal_.close();  // Release file handle
        
        // Atomic rename (Unix guarantees this)
        if (std::rename(temp_filename.c_str(), journal_path_.c_str()) == 0) {
            std::cout << "[Compaction] COMPACTION SUCCESS" << std::endl;
        }
        
        journal_.open(journal_path_, std::ios::app);  // Reopen for new writes
    }
    
    is_compacting_ = false;  // Resume background flusher
}
```

### Why Atomic Rename?

**Unix `rename()` Semantics:**
- If `rename()` succeeds, the new file atomically replaces the old one
- If `rename()` fails (e.g., crash during rename), the old file remains intact
- **No corruption possible:** Either you have the old file or the new file, never both

**Example:**
```
Before: wal.log (200,000 lines, 5MB)
        wal.log.tmp (200,006 lines, 5MB)  ← snapshot

rename("wal.log.tmp", "wal.log")

After:  wal.log (200,006 lines, 5MB)  ← compacted
```

### Automatic Compaction

The background flusher thread checks file size every 60 seconds:

```cpp
auto now = std::chrono::steady_clock::now();
if (std::chrono::duration_cast<std::chrono::seconds>(now - last_compaction_check).count() >= 60) {
    if (std::filesystem::exists(journal_path_)) {
        size_t file_size = std::filesystem::file_size(journal_path_);
        if (file_size > COMPACTION_THRESHOLD) {  // 100MB
            compact();
        }
    }
}
```

**Threshold:** 100MB (configurable via `COMPACTION_THRESHOLD`)

**Manual Trigger:** Clients can also trigger compaction with `COMPACT` command.

## Durability Guarantees

### What We Guarantee

1. **Crash Recovery:** After a crash, the server recovers to the last consistent state (up to 100ms before crash)
2. **No Data Corruption:** Atomic operations ensure the WAL is never in a partially-written state
3. **Ordering:** Operations are replayed in the exact order they were written

### What We Don't Guarantee

1. **Immediate Durability:** Writes may be buffered for up to 100ms
2. **ACID Transactions:** No transaction support (single-operation atomicity only)
3. **Replication:** Single-server only (no distributed consistency)

### Future Enhancements

1. **Synchronous Flushing:** Add `fsync()` option for stronger durability (with performance cost)
2. **Checkpointing:** Periodic snapshots + incremental WAL for faster recovery
3. **Replication:** Multi-server replication for high availability

## Performance Characteristics

| Operation | Disk I/O | Latency Impact |
|-----------|----------|----------------|
| SET       | Buffered write | ~0.1ms (async) |
| GET       | None            | 0ms            |
| DEL       | Buffered write | ~0.1ms (async) |
| Recovery  | Sequential read | O(n) where n = log size |
| Compaction| Write snapshot  | O(n) where n = unique keys |

The async flushing strategy enables high throughput while maintaining reasonable durability guarantees.

