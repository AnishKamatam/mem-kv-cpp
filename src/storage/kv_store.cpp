#include "kv_store.h"
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

class KVStore::Impl {
public:
    std::unordered_map<std::string, std::string> data_;
    std::mutex mtx_;
    std::ofstream journal_;
    
    Impl(const std::string& filename) {
        // Create directory if it doesn't exist
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
    }
    
    ~Impl() {
        if (journal_.is_open()) {
            journal_.close();
        }
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
                    data_[key] = value;
                }
            } 
            else if (cmd == "DEL") {
                std::string key;
                ss >> key;
                if (!key.empty()) {
                    data_.erase(key);
                }
            }
        }
    }
    
    void set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_[key] = value;
        
        if (journal_.is_open()) {
            journal_ << "SET " << key << " " << value << "\n";
            journal_.flush();
        }
    }
    
    std::string get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_.count(key) ? data_[key] : "(nil)";
    }
    
    bool del(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        bool existed = data_.erase(key) > 0;
        
        if (journal_.is_open() && existed) {
            journal_ << "DEL " << key << "\n";
            journal_.flush();
        }
        
        return existed;
    }
};

KVStore::KVStore(const std::string& filename) : filename_(filename), impl_(new Impl(filename)) {}

KVStore::~KVStore() {
    delete impl_;
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
            
        default:
            return "ERROR: Unknown command\n";
    }
}

