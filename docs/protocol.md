# Protocol Specification

This document defines the communication protocol used by the mem-kv-cpp server. The server supports both plain-text and RESP (Redis Serialization Protocol) formats for compatibility with different clients.

## Protocol Overview

The server accepts commands over TCP on port 8080 (configurable). Each command is terminated by a newline character (`\n`). Responses are also newline-terminated.

## Supported Commands

### SET

**Purpose:** Store a key-value pair in the database. Supports optional TTL for ML inference caching.

**Plain-Text Format:**
```
SET <key> <value> [EX <seconds>]\n
```

**RESP Format:**
```
*3\r\n$3\r\nSET\r\n$<key_len>\r\n<key>\r\n$<value_len>\r\n<value>\r\n
*4\r\n$3\r\nSET\r\n$<key_len>\r\n<key>\r\n$<value_len>\r\n<value>\r\n$2\r\nEX\r\n$<ttl>\r\n<seconds>\r\n
```

**Response:**
```
OK\n
```

**Example:**
```bash
$ echo "SET name Anish" | nc localhost 8080
OK

$ echo "SET model:user:123 prediction EX 3600" | nc localhost 8080
OK
```

**Behavior:**
- If the key already exists, the value is overwritten
- Keys and values are stored as strings
- TTL (Time-To-Live) specified in seconds (e.g., `EX 3600` = 1 hour)
- Entries with TTL expire automatically after specified time
- No size limit (subject to available memory)
- Write operations are batched for performance (immediate "OK" response)

**Time Complexity:** O(1) average

**ML Use Case:** Cache ML inference results with expiration to reduce GPU compute costs.

---

### GET

**Purpose:** Retrieve the value associated with a key.

**Plain-Text Format:**
```
GET <key>\n
```

**RESP Format:**
```
*2\r\n$3\r\nGET\r\n$<key_len>\r\n<key>\r\n
```

**Response (Key Exists):**
```
<value>\n
```

**Response (Key Not Found):**
```
(nil)\n
```

**Example:**
```bash
$ echo "GET name" | nc localhost 8080
Anish

$ echo "GET nonexistent" | nc localhost 8080
(nil)
```

**Behavior:**
- Returns the current value of the key
- Returns `(nil)` if the key does not exist
- Read-only operation (no WAL write)

**Time Complexity:** O(1) average

---

### DEL

**Purpose:** Delete a key-value pair from the database.

**Plain-Text Format:**
```
DEL <key>\n
```

**RESP Format:**
```
*2\r\n$3\r\nDEL\r\n$<key_len>\r\n<key>\r\n
```

**Response:**
```
OK\n
```

**Example:**
```bash
$ echo "DEL name" | nc localhost 8080
OK

$ echo "GET name" | nc localhost 8080
(nil)
```

**Behavior:**
- Deletes the key if it exists
- No error if the key does not exist
- Writes deletion to WAL for durability

**Time Complexity:** O(1) average

---

### MGET

**Purpose:** Retrieve multiple values in a single request. Optimized for ML feature vector retrieval.

**Plain-Text Format:**
```
MGET <key1> <key2> <key3> ...\n
```

**RESP Format:**
```
*<n+1>\r\n$4\r\nMGET\r\n$<key1_len>\r\n<key1>\r\n$<key2_len>\r\n<key2>\r\n...
```

**Response:**
```
<value1> <value2> <value3> ...\n
```

**Example:**
```bash
$ echo "SET user:age 25" | nc localhost 8080
OK
$ echo "SET user:location NYC" | nc localhost 8080
OK
$ echo "MGET user:age user:location user:preferences" | nc localhost 8080
25 NYC (nil)
```

**Behavior:**
- Returns values in the same order as requested keys
- Returns `(nil)` for missing or expired keys
- Shard-aware: Groups keys by shard to minimize lock acquisitions
- Latency tracked in metrics histogram

**Time Complexity:** O(k) where k = number of keys

**ML Use Case:** Retrieve multiple user features for model inference in a single network round-trip.

### STATS

**Purpose:** Get performance metrics and latency histograms for ML observability.

**Plain-Text Format:**
```
STATS\n
```

**Response:**
```json
{
  "cache_hits": 850,
  "cache_misses": 150,
  "total_requests": 1000,
  "hit_rate": 85.0,
  "avg_latency_us": 1250.5,
  "p50_latency_us": 800,
  "p95_latency_us": 3500,
  "p99_latency_us": 8500,
  "p50_less_than_1ms": 920,
  "p99_tail_events": 5,
  "batch_avg_size": 42.5,
  "histogram": {
    "<1ms": 920,
    "<5ms": 50,
    "<10ms": 20,
    "<50ms": 5,
    "<100ms": 2,
    ">=100ms": 3
  }
}
```

**Example:**
```bash
$ echo "STATS" | nc localhost 8080
{"cache_hits":100,"cache_misses":0,"total_requests":100,...}
```

**Behavior:**
- Returns JSON-formatted metrics
- Includes cache hit/miss rates
- Provides latency percentiles (P50, P95, P99)
- Shows histogram distribution
- Tracks batch statistics

**ML Use Case:** Monitor cache performance and ensure SLA compliance (P99 < 10ms).

### COMPACT

**Purpose:** Trigger log compaction to reduce WAL file size.

**Plain-Text Format:**
```
COMPACT\n
```

**RESP Format:**
```
*1\r\n$7\r\nCOMPACT\r\n
```

**Response:**
```
OK\n
```

**Example:**
```bash
$ echo "COMPACT" | nc localhost 8080
OK
```

**Behavior:**
- Creates a snapshot of current database state
- Atomically replaces the WAL file with the compacted version
- Removes duplicate entries (keeps only latest value per key)
- Skips expired TTL entries during compaction
- Blocks briefly while creating snapshot (locks all shards)

**Time Complexity:** O(n) where n = number of unique keys

**Note:** Compaction also runs automatically when the WAL exceeds 100MB.

---

## RESP (Redis Serialization Protocol)

The server supports RESP for compatibility with Redis clients and `redis-benchmark`.

### RESP Format Overview

RESP uses a simple binary protocol:
- `*<n>`: Array with n elements
- `$<n>`: Bulk string of length n
- `\r\n`: Line terminator

### Example: SET Command in RESP

**Client sends:**
```
*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nAnish\r\n
```

**Breakdown:**
- `*3`: Array with 3 elements
- `$3\r\nSET\r\n`: First element is "SET" (3 characters)
- `$4\r\nname\r\n`: Second element is "name" (4 characters)
- `$5\r\nAnish\r\n`: Third element is "Anish" (5 characters)

**Server responds:**
```
+OK\r\n
```

### RESP Response Types

- `+<string>\r\n`: Simple string (e.g., `+OK\r\n`)
- `-<error>\r\n`: Error message
- `$<len>\r\n<data>\r\n`: Bulk string
- `$-1\r\n`: Null bulk string (for `(nil)`)

## Error Handling

### Unknown Command

**Request:**
```
INVALID\n
```

**Response:**
```
ERROR: Unknown command\n
```

### Malformed Command

**Request:**
```
SET\n
```

**Response:**
```
ERROR: Unknown command\n
```

**Note:** The parser is lenient and attempts to extract valid commands from malformed input when possible.

## Connection Lifecycle

1. **Connection:** Client opens TCP connection to server
2. **Commands:** Client sends one or more commands (each terminated by `\n`)
3. **Responses:** Server responds to each command immediately
4. **Disconnection:** Client closes connection (or server closes on error)

**Persistent Connections:** Clients can send multiple commands over the same connection without reconnecting.

## Protocol Detection

The server automatically detects the protocol format:

- **RESP:** Commands starting with `*` (array indicator)
- **Plain-Text:** All other commands

This allows the server to work with both simple `nc` clients and Redis-compatible tools.

## Example Session

```bash
$ nc localhost 8080
SET user_1 Alice
OK
SET user_2 Bob
OK
GET user_1
Alice
GET user_2
Bob
DEL user_1
OK
GET user_1
(nil)
COMPACT
OK
^C
```

## Compatibility

### Redis Clients

The server is compatible with:
- `redis-cli` (with RESP format)
- `redis-benchmark` (for performance testing)
- Any RESP-compatible client library

### Simple Clients

The server works with:
- `netcat` (`nc`)
- `telnet`
- Custom TCP clients

## Future Protocol Enhancements

1. **Pipelining:** Support multiple commands in a single request
2. **Binary Protocol:** More efficient encoding for high-throughput scenarios
3. **Authentication:** Add password protection
4. **TLS/SSL:** Encrypted connections
5. **Pub/Sub:** Publish-subscribe messaging

