#pragma once

#include "command.h"
#include <string>

class Parser {
public:
    static ParsedCommand parse(const std::string& input);
};

