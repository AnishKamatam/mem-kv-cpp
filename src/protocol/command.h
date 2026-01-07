#pragma once

#include <string>
#include <vector>

enum class CommandType { SET, GET, DEL, COMPACT, STATS, MGET, UNKNOWN };

struct ParsedCommand {
    CommandType type;
    std::string key;
    std::string value;
    std::vector<std::string> keys; // For MGET
    int ttl_seconds = 0; // New: ML Cache TTL
    bool valid = true;
};

