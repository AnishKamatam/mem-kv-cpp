#pragma once

#include <string>

enum class CommandType { SET, GET, DEL, UNKNOWN };

struct ParsedCommand {
    CommandType type;
    std::string key;
    std::string value;
    bool valid = true;
};

