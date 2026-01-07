#include "kv_store.h"
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

struct Shard {
    std::unordered_map<std::string, std::string> data;
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
            
            std::stringstream ss(line);
            std::string cmd;
            ss >> cmd;
            
            if (cmd == "SET") {
                std::string key, value;
                ss >> key;
                std::getline(ss >> std::ws, value);
                if (!key.empty()) {
                    size_t idx = get_shard_index(key);
                    shards[idx].data[key] = value;
                }
            } 
            else if (cmd == "DEL") {
                std::string key;
                ss >> key;
                if (!key.empty()) {
                    size_t idx = get_shard_index(key);
                    shards[idx].data.erase(key);
                }
            }
        }
    }
    
    void set(const std::string& key, const std::string& value) {
        size_t idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shards[idx].mtx);
        
        shards[idx].data[key] = value;
        
        if (journal_.is_open()) {
            std::lock_guard<std::mutex> journal_lock(journal_mtx_);
            journal_ << "SET " << key << " " << value << "\n";
        }
    }
    
    std::string get(const std::string& key) {
        size_t idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shards[idx].mtx);
        
        auto it = shards[idx].data.find(key);
        return (it != shards[idx].data.end()) ? it->second : "(nil)";
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
        std::cout << "[Compaction] Starting..." << std::endl;
        is_compacting_ = true;
        
        std::string temp_filename = journal_path_ + ".tmp";
        std::cout << "[Compaction] Temp file: " << temp_filename << std::endl;
        
        size_t total_keys = 0;
        {
            std::ofstream temp_journal(temp_filename, std::ios::trunc);
            if (!temp_journal.is_open()) {
                std::cerr << "[Compaction] ERROR: Could not open temp file" << std::endl;
                is_compacting_ = false;
                return;
            }
            
            for (size_t i = 0; i < NUM_SHARDS; ++i) {
                std::lock_guard<std::mutex> lock(shards[i].mtx);
                
                for (const auto& [key, value] : shards[i].data) {
                    temp_journal << "SET " << key << " " << value << "\n";
                    total_keys++;
                }
            }
            
            temp_journal.close();
        }
        
        std::cout << "[Compaction] Snapshot created with " << total_keys << " keys" << std::endl;
        
        {
            std::lock_guard<std::mutex> lock(journal_mtx_);
            
            if (journal_.is_open()) {
                journal_.close();
                std::cout << "[Compaction] Journal closed" << std::endl;
            }
            
            if (std::rename(temp_filename.c_str(), journal_path_.c_str()) != 0) {
                std::cerr << "[Compaction] RENAME ERROR: " << strerror(errno) << std::endl;
            } else {
                std::cout << "[Compaction] COMPACTION SUCCESS" << std::endl;
            }
            
            journal_.open(journal_path_, std::ios::app);
            if (!journal_.is_open()) {
                std::cerr << "[Compaction] ERROR: Could not reopen journal" << std::endl;
            }
        }
        
        is_compacting_ = false;
        std::cout << "[Compaction] Complete" << std::endl;
    }
};

KVStore::KVStore(const std::string& filename) : filename_(filename), impl_(new Impl(filename)) {}

KVStore::~KVStore() {
    delete impl_;
}

void KVStore::compact() {
    impl_->compact();
}

std::string KVStore::execute(const ParsedCommand& cmd) {
    if (!cmd.valid) {
        return "ERROR: Unknown command\n";
    }
    
    switch (cmd.type) {
        case CommandType::SET:
            impl_->set(cmd.key, cmd.value);
            return "OK\n";
            
        case CommandType::GET:
            return impl_->get(cmd.key) + "\n";
            
        case CommandType::DEL:
            impl_->del(cmd.key);
            return "OK\n";
            
        case CommandType::COMPACT:
            impl_->compact();
            return "OK\n";
            
        default:
            return "ERROR: Unknown command\n";
    }
}

