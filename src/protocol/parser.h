#pragma once

#include "command.h"
#include <string>

class Parser {
public:
    static ParsedCommand parse(const std::string& input);
    
private:
    static ParsedCommand parse_plain_text(const std::string& input);
    static ParsedCommand parse_resp(const std::string& input);
};

