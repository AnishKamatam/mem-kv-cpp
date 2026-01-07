#pragma once

#include <string>

enum class CommandType { SET, GET, DEL, COMPACT, UNKNOWN };

struct ParsedCommand {
    CommandType type;
    std::string key;
    std::string value;
    bool valid = true;
};

