#include "kv_store.h"
#include "../metrics/metrics.h"
#include "../protocol/parser.h"
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>

struct CacheEntry {
    std::string value;
    long long expiry_at_ms = 0; // Unix timestamp in ms. 0 = permanent.
    
    bool is_expired() const {
        if (expiry_at_ms == 0) return false;
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        return now_ms > expiry_at_ms;
    }
};

struct Shard {
    std::unordered_map<std::string, CacheEntry> data;
    std::mutex mtx;
};

class KVStore::Impl {
public:
    static constexpr size_t NUM_SHARDS = 16;
    Shard shards[NUM_SHARDS];
    std::ofstream journal_;
    std::mutex journal_mtx_;
    std::atomic<bool> running_{true};
    std::atomic<bool> is_compacting_{false};
    std::thread flusher_thread_;
    std::string journal_path_;
    static constexpr size_t COMPACTION_THRESHOLD = 100 * 1024 * 1024;
    
    Impl(const std::string& filename) : journal_path_(filename) {
        std::filesystem::path file_path(filename);
        std::filesystem::path dir_path = file_path.parent_path();
        if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
            std::filesystem::create_directories(dir_path);
        }
        
        load_from_disk(filename);
        journal_.open(filename, std::ios::app);
        if (!journal_.is_open()) {
            std::cerr << "Warning: Could not open journal file: " << filename << std::endl;
        }
        
        flusher_thread_ = std::thread([this]() {
            auto last_compaction_check = std::chrono::steady_clock::now();
            
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                if (is_compacting_) continue;
                
                {
                    std::lock_guard<std::mutex> lock(journal_mtx_);
                    if (journal_.is_open()) {
                        journal_.flush();
                    }
                }
                
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_compaction_check).count() >= 60) {
                    last_compaction_check = now;
                    
                    if (std::filesystem::exists(journal_path_)) {
                        size_t file_size = std::filesystem::file_size(journal_path_);
                        if (file_size > COMPACTION_THRESHOLD) {
                            compact();
                        }
                    }
                }
            }
        });
    }
    
    ~Impl() {
        running_ = false;
        if (flusher_thread_.joinable()) {
            flusher_thread_.join();
        }
        
        if (journal_.is_open()) {
            journal_.flush();
            journal_.close();
        }
    }
    
    size_t get_shard_index(const std::string& key) {
        return std::hash<std::string>{}(key) % NUM_SHARDS;
    }
    
    void load_from_disk(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            ParsedCommand cmd = Parser::parse(line);
            
            if (cmd.type == CommandType::SET) {
                size_t idx = get_shard_index(cmd.key);
                std::lock_guard<std::mutex> lock(shards[idx].mtx);
                
                CacheEntry entry;
                entry.value = cmd.value;
                if (cmd.ttl_seconds > 0) {
                    // On load, expired entries will be evicted on first access
                    auto now = std::chrono::system_clock::now().time_since_epoch();
                    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                    entry.expiry_at_ms = now_ms + (cmd.ttl_seconds * 1000LL);
                } else {
                    entry.expiry_at_ms = 0;
                }
                
                shards[idx].data[cmd.key] = entry;
            }
            else if (cmd.type == CommandType::DEL) {
                size_t idx = get_shard_index(cmd.key);
                std::lock_guard<std::mutex> lock(shards[idx].mtx);
                shards[idx].data.erase(cmd.key);
            }
        }
    }
    
    void set(const std::string& key, const std::string& value, int ttl_seconds = 0) {
        size_t idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shards[idx].mtx);
        
        CacheEntry entry;
        entry.value = value;
        
        if (ttl_seconds > 0) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            entry.expiry_at_ms = now_ms + (ttl_seconds * 1000LL);
        } else {
            entry.expiry_at_ms = 0; // Permanent
        }
        
        shards[idx].data[key] = entry;
        
        if (journal_.is_open()) {
            std::lock_guard<std::mutex> journal_lock(journal_mtx_);
            journal_ << "SET " << key << " " << value;
            if (ttl_seconds > 0) {
                journal_ << " EX " << ttl_seconds;
            }
            journal_ << "\n";
        }
    }
    
    std::string get(const std::string& key) {
        auto start = std::chrono::high_resolution_clock::now();
        
        size_t idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shards[idx].mtx);
        
        Metrics::instance().total_requests++;
        
        auto it = shards[idx].data.find(key);
        if (it == shards[idx].data.end()) {
            Metrics::instance().cache_misses++;
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            Metrics::instance().record_latency(duration.count());
            
            return "(nil)";
        }
        
        // TTL Eviction Logic for Inference Results
        if (it->second.is_expired()) {
            shards[idx].data.erase(it); // Lazy eviction
            Metrics::instance().cache_misses++;
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            Metrics::instance().record_latency(duration.count());
            
            return "(nil)";
        }
        
        Metrics::instance().cache_hits++;
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        Metrics::instance().record_latency(duration.count());
        
        return it->second.value;
    }
    
    std::vector<std::string> mget(const std::vector<std::string>& keys) {
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<std::string> results;
        results.reserve(keys.size());
        
        // Group keys by shard to minimize lock acquisitions, but preserve order
        std::unordered_map<size_t, std::vector<size_t>> shard_to_indices;
        std::vector<size_t> shard_order;
        
        for (size_t i = 0; i < keys.size(); ++i) {
            size_t idx = get_shard_index(keys[i]);
            if (shard_to_indices.find(idx) == shard_to_indices.end()) {
                shard_order.push_back(idx);
            }
            shard_to_indices[idx].push_back(i);
        }
        
        // Pre-allocate results
        results.resize(keys.size());
        
        // Process each shard group (in order of first appearance)
        for (size_t shard_idx : shard_order) {
            std::lock_guard<std::mutex> lock(shards[shard_idx].mtx);
            
            for (size_t key_idx : shard_to_indices[shard_idx]) {
                const auto& key = keys[key_idx];
                auto it = shards[shard_idx].data.find(key);
                if (it == shards[shard_idx].data.end()) {
                    results[key_idx] = "(nil)";
                } else if (it->second.is_expired()) {
                    shards[shard_idx].data.erase(it);
                    results[key_idx] = "(nil)";
                } else {
                    results[key_idx] = it->second.value;
                }
            }
        }
        
        // Record latency for MGET
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        Metrics::instance().record_latency(duration.count());
        
        return results;
    }
    
    bool del(const std::string& key) {
        size_t idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shards[idx].mtx);
        bool existed = shards[idx].data.erase(key) > 0;
        
        if (journal_.is_open() && existed) {
            std::lock_guard<std::mutex> journal_lock(journal_mtx_);
            journal_ << "DEL " << key << "\n";
        }
        
        return existed;
    }
    
    void compact() {
        is_compacting_ = true;
        
        std::string temp_filename = journal_path_ + ".tmp";
        
        {
            std::ofstream temp_journal(temp_filename, std::ios::trunc);
            if (!temp_journal.is_open()) {
                std::cerr << "Warning: Could not open temp file for compaction" << std::endl;
                is_compacting_ = false;
                return;
            }
            
            for (size_t i = 0; i < NUM_SHARDS; ++i) {
                std::lock_guard<std::mutex> lock(shards[i].mtx);
                
                for (const auto& [key, entry] : shards[i].data) {
                    if (!entry.is_expired()) {
                        temp_journal << "SET " << key << " " << entry.value << "\n";
                    }
                }
            }
            
            temp_journal.close();
        }
        
        {
            std::lock_guard<std::mutex> lock(journal_mtx_);
            
            if (journal_.is_open()) {
                journal_.close();
            }
            
            if (std::rename(temp_filename.c_str(), journal_path_.c_str()) != 0) {
                std::cerr << "Warning: Failed to rename temp journal during compaction: " << strerror(errno) << std::endl;
            }
            
            journal_.open(journal_path_, std::ios::app);
            if (!journal_.is_open()) {
                std::cerr << "Warning: Could not reopen journal after compaction" << std::endl;
            }
        }
        
        is_compacting_ = false;
    }
};

KVStore::KVStore(const std::string& filename) : filename_(filename), impl_(new Impl(filename)) {}

KVStore::~KVStore() {
    delete impl_;
}

void KVStore::compact() {
    impl_->compact();
}

std::vector<std::string> KVStore::mget(const std::vector<std::string>& keys) {
    return impl_->mget(keys);
}

std::string KVStore::execute(const ParsedCommand& cmd) {
    if (!cmd.valid) {
        return "ERROR: Unknown command\n";
    }
    
    switch (cmd.type) {
        case CommandType::SET:
            impl_->set(cmd.key, cmd.value, cmd.ttl_seconds);
            return "OK\n";
            
        case CommandType::GET:
            return impl_->get(cmd.key) + "\n";
            
        case CommandType::MGET: {
            auto results = impl_->mget(cmd.keys);
            std::string response;
            for (size_t i = 0; i < results.size(); ++i) {
                if (i > 0) response += " ";
                response += results[i];
            }
            return response + "\n";
        }
            
        case CommandType::DEL:
            impl_->del(cmd.key);
            return "OK\n";
            
        case CommandType::COMPACT:
            impl_->compact();
            return "OK\n";
            
        case CommandType::STATS:
            return Metrics::instance().to_json() + "\n";
            
        default:
            return "ERROR: Unknown command\n";
    }
}

