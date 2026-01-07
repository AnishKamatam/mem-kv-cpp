#pragma once

#include <string>
#include "../protocol/command.h"

class KVStore {
public:
    KVStore(const std::string& filename);
    ~KVStore();
    
    std::string execute(const ParsedCommand& cmd);
    void compact();
    std::vector<std::string> mget(const std::vector<std::string>& keys);

private:
    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    bool del(const std::string& key);
    
    std::string filename_;
    void load_from_disk(const std::string& filename);
    
    class Impl;
    Impl* impl_;
};

