#include "write_batcher.h"
#include "../storage/kv_store.h"
#include "../metrics/metrics.h"
#include <iostream>

WriteBatcher::WriteBatcher(KVStore& store) : store_(store) {
    flusher_thread_ = std::thread([this]() { background_flusher(); });
}

WriteBatcher::~WriteBatcher() {
    running_ = false;
    if (flusher_thread_.joinable()) {
        flusher_thread_.join();
    }
    // Flush any remaining commands
    flush_to_store();
}

void WriteBatcher::add_to_batch(const ParsedCommand& cmd) {
    // Only batch SET and DEL commands (writes)
    if (cmd.type != CommandType::SET && cmd.type != CommandType::DEL) {
        // Execute non-write commands immediately
        store_.execute(cmd);
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(batch_mtx_);
        current_batch_.commands.push_back(cmd);
        
        // Flush if batch is full
        if (current_batch_.size() >= BATCH_SIZE_THRESHOLD) {
            flush_to_store();
        }
    }
}

void WriteBatcher::flush_to_store() {
    Batch batch_to_flush;
    
    {
        std::lock_guard<std::mutex> lock(batch_mtx_);
        if (current_batch_.commands.empty()) {
            return;
        }
        batch_to_flush = current_batch_;
        current_batch_.clear();
    }
    
    // Record batch statistics
    Metrics::instance().record_batch(batch_to_flush.size());
    
    // Apply all commands in batch
    for (const auto& cmd : batch_to_flush.commands) {
        store_.execute(cmd);
    }
}

void WriteBatcher::background_flusher() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FLUSH_INTERVAL_MS));
        flush_to_store();
    }
}

