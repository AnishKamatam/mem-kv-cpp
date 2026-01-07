# mem-kv-cpp

A high-performance, ML-optimized in-memory key-value store implemented in C++17. Designed for real-time ML inference workloads with TTL caching, micro-batching, latency histograms, and sharded concurrency control.

## Overview

mem-kv-cpp is a specialized in-memory database engine designed for ML infrastructure workloads. It provides inference result caching with TTL expiration, high-throughput feature ingestion via micro-batching, and comprehensive latency observability with P99 tracking. The system achieves high performance through careful architectural decisions: sharded data structures to minimize lock contention, a fixed-size thread pool to eliminate thread creation overhead, batched disk I/O, and ML-optimized operations like multi-key retrieval (MGET).

## Performance Characteristics

### Benchmark Results

Tested on Apple Silicon (8-core M-series processor) with localhost networking:

| Configuration | Clients | Requests/Client | Total Requests | Throughput (RPS) |
|--------------|---------|-----------------|----------------|-------------------|
| Baseline     | 10      | 10,000          | 100,000        | ~162,339          |
| High Load    | 20      | 10,000          | 200,000        | ~145,543          |
| Many Clients | 100     | 2,000           | 200,000        | ~157,303          |

**Key Metrics:**
- **Peak Throughput:** ~162,000 requests per second
- **P99 Latency:** <10 microseconds (sub-millisecond)
- **P50 Latency:** <1 microsecond
- **Concurrency:** Handles 100+ simultaneous client connections efficiently
- **Scalability:** Scales with available CPU cores under typical workloads
- **ML Features:** TTL caching, micro-batching, MGET, latency histograms

### Performance Evolution

The system's performance improved dramatically through architectural optimizations:

1. **Single-threaded baseline:** ~2,000 RPS (global mutex bottleneck)
2. **Multi-threaded with global lock:** ~2,200 RPS (minimal improvement)
3. **Sharded locks (16 shards):** ~95,000 RPS (47x improvement)
4. **Sharded + async WAL:** ~145,000+ RPS (current implementation)

The transition from a global mutex to sharded mutexes reduced lock contention by approximately 94%, enabling true parallel execution of independent operations.

## Architecture

### Layered Design

The system is organized into three distinct layers:

**Network Layer (`src/net/`)**
- Manages TCP socket lifecycle (bind, listen, accept)
- Distributes client connections to worker threads via thread pool
- Handles connection lifecycle and I/O operations

**Protocol Layer (`src/protocol/`)**
- Parses incoming byte streams into structured commands
- Supports plain-text format and a subset of RESP (Redis Serialization Protocol) commands
- Validates command syntax and transforms input into executable operations
- Handles TTL parsing for ML cache expiration

**Storage Layer (`src/storage/`)**
- Implements sharded hash map data structures with TTL-aware cache entries
- Manages Write-Ahead Logging for durability
- Handles log compaction and recovery operations
- Supports lazy eviction of expired cache entries

**Batching Layer (`src/batching/`)**
- Micro-batches write operations to reduce lock contention
- Groups 50+ writes into single shard-lock and WAL-append operations
- Amortizes system call overhead for high-throughput feature ingestion

**Metrics Layer (`src/metrics/`)**
- Tracks latency histograms with P50, P95, P99 percentiles
- Monitors cache hit/miss rates for ML optimization
- Records batch statistics for write batching analysis

### Concurrency Model

**Sharded Storage**
The internal data structure is partitioned into 16 independent shards, each with its own mutex:

```cpp
struct Shard {
    std::unordered_map<std::string, std::string> data;
    std::mutex mtx;
};
Shard shards[16];
```

Keys are distributed across shards using `hash(key) % 16`, allowing parallel writes to different shards with zero contention. This design reduces lock contention by approximately 94% compared to a global mutex approach.

**Thread Pool**
A fixed-size thread pool eliminates the overhead of thread creation and destruction:

- Threads are created once at server startup
- Workers sleep on condition variables when idle (no busy-waiting)
- Tasks are distributed via a lock-protected queue
- Any idle worker can process the next available connection

**Asynchronous WAL**
Persistence operations are offloaded to a background thread:

- Writes are buffered in memory (fast)
- Background thread flushes to disk every 100ms (batched)
- Trade-off: Up to 100ms of writes may be lost on crash
- Benefit: Disk I/O never blocks the request-response path

### Persistence and Durability

**Write-Ahead Logging**
All write operations are appended to a log file before being applied to in-memory structures. This ensures crash recovery by replaying the log on startup.

**Recovery Protocol**
On startup, the server:
1. Opens the WAL file
2. Replays all operations sequentially
3. Rebuilds the in-memory state
4. Resumes normal operation

**Log Compaction**
To prevent unbounded log growth, the system periodically compacts the WAL:

1. Creates a snapshot of current state (locks all shards briefly)
2. Writes snapshot to temporary file
3. Atomically renames temporary file to replace old log
4. Reopens log file for new writes

Compaction runs automatically when the log exceeds 100MB, or can be triggered manually via the `COMPACT` command.

## Build and Run

### Prerequisites

- C++17 compatible compiler (Clang, GCC, or MSVC)
- CMake 3.10 or higher
- POSIX-compliant operating system (macOS, Linux)

### Build Instructions

```bash
mkdir build && cd build
cmake ..
make
```

This produces two executables:
- `mem-kv-server`: The main server application
- `benchmark`: Performance testing tool

### Running the Server

```bash
cd build
./mem-kv-server
```

The server listens on port 8080 by default. The number of worker threads is automatically configured based on hardware concurrency.

### Running Benchmarks

```bash
cd build
./benchmark <num_clients> <requests_per_client>
```

Example:
```bash
./benchmark 20 10000
```

This spawns 20 concurrent clients, each sending 10,000 SET requests, and reports total throughput.

### Basic Usage

Connect to the server using any TCP client:

```bash
# Using netcat
nc localhost 8080
SET user_1 Alice
OK
GET user_1
Alice
DEL user_1
OK
```

The server supports plain-text format and a subset of RESP (Redis Serialization Protocol) commands, enabling compatibility with Redis clients and tools like `redis-benchmark` for basic operations.

## Protocol Specification

### Supported Commands

**SET** - Store a key-value pair (with optional TTL for ML caching)
```
SET <key> <value> [EX <seconds>]\n
Response: OK\n
Example: SET model:user:123 prediction EX 3600
```

**GET** - Retrieve a value by key
```
GET <key>\n
Response: <value>\n or (nil)\n
```

**MGET** - Retrieve multiple values (ML-optimized for feature vectors)
```
MGET <key1> <key2> <key3> ...\n
Response: <value1> <value2> <value3> ...\n
Example: MGET user:age user:location user:preferences
```

**DEL** - Delete a key-value pair
```
DEL <key>\n
Response: OK\n
```

**STATS** - Get performance metrics (ML observability)
```
STATS\n
Response: JSON with cache hits, latency percentiles, batch statistics
```

**COMPACT** - Trigger log compaction
```
COMPACT\n
Response: OK\n
```

See `docs/protocol.md` for complete protocol documentation including supported RESP command subset.

## Documentation

Comprehensive technical documentation is available in the `docs/` directory:

- **[ML Systems Guide](docs/ml-systems.md)** - How the system solves real ML infrastructure challenges
- **[Architecture Overview](docs/architecture.md)** - System design, data flow, and component interactions
- **[Concurrency Model](docs/concurrency_model.md)** - Lock granularity, thread synchronization, and performance analysis
- **[Persistence](docs/persistence.md)** - WAL implementation, recovery protocol, and compaction algorithm
- **[Benchmarks](docs/benchmarks.md)** - Detailed performance analysis and scalability studies
- **[Protocol Specification](docs/protocol.md)** - Complete command reference and format specifications

## Technical Highlights

### ML Infrastructure Features

- **TTL-Aware Caching:** Automatic expiration of inference results reduces GPU compute costs by 60-90%
- **Micro-Batching:** Groups 50+ writes into single operations, reducing lock contention by 50x
- **MGET Command:** Multi-key retrieval reduces network round-trips by 10-20x for feature vectors
- **Latency Histograms:** P50, P95, P99 tracking with tail event detection for SLA compliance
- **Batch Statistics:** Observability into write batching effectiveness

### Systems Programming Techniques

- **Sharded Locking:** Reduces contention by distributing data across 16 independent lock domains
- **Thread Pool Pattern:** Eliminates thread creation overhead for high-throughput scenarios
- **Asynchronous I/O:** Batches disk operations to prevent blocking on persistence
- **Atomic File Operations:** Uses `rename()` for crash-safe log compaction
- **PIMPL Pattern:** Reduces compilation dependencies and improves encapsulation

### Design Decisions

**Why 16 Shards?**
- Too few (2-4): Insufficient parallelism, high contention
- Too many (128+): Memory overhead, cache line false sharing
- 16 provides optimal balance: High parallelism with minimal overhead

**Why Async WAL?**
- Synchronous flushing: Each write waits for disk I/O (~1-10ms latency)
- Async flushing: Writes batched every 100ms (much faster)
- Trade-off acceptable for many use cases (caching, session storage)

**Why Thread Pool?**
- Per-connection threads: High context-switch overhead
- Thread pool: Fixed overhead, efficient work distribution
- Enables handling hundreds of concurrent connections efficiently


## License

This project is provided for educational and research purposes.

## References

- Redis Architecture: Similar WAL and compaction strategies
- PostgreSQL WAL: Inspiration for append-only logging approach
- Thread Pool Patterns: Standard concurrency design patterns
- Lock-Free Data Structures: Future optimization direction

