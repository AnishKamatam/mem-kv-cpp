#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include "../protocol/command.h"

// Forward declaration
class KVStore;

struct Batch {
    std::vector<ParsedCommand> commands;
    
    size_t size() const { return commands.size(); }
    
    void clear() { commands.clear(); }
};

class WriteBatcher {
public:
    WriteBatcher(KVStore& store);
    ~WriteBatcher();
    
    void add_to_batch(const ParsedCommand& cmd);
    void flush_to_store();
    
private:
    void background_flusher();
    
    KVStore& store_;
    std::mutex batch_mtx_;
    Batch current_batch_;
    std::atomic<bool> running_{true};
    std::thread flusher_thread_;
    
    static constexpr size_t BATCH_SIZE_THRESHOLD = 50;
    static constexpr size_t FLUSH_INTERVAL_MS = 10;
};

