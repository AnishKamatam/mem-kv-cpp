#pragma once

#include <string>
#include "../protocol/command.h"

class KVStore {
public:
    KVStore(const std::string& filename);
    ~KVStore();
    
    std::string execute(const ParsedCommand& cmd);

private:
    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    bool del(const std::string& key);
    
    std::string filename_;
    void load_from_disk(const std::string& filename);
    
    // These will be in the .cpp file
    class Impl;
    Impl* impl_;
};

