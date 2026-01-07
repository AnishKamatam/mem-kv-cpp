#include "parser.h"
#include <sstream>

ParsedCommand Parser::parse(const std::string& input) {
    std::stringstream ss(input);
    std::string cmd_name;
    ss >> cmd_name;
    
    ParsedCommand cmd;
    if (cmd_name == "SET") {
        cmd.type = CommandType::SET;
        ss >> cmd.key;
        // Use getline to handle values with spaces
        std::getline(ss >> std::ws, cmd.value);
    } 
    else if (cmd_name == "GET") {
        cmd.type = CommandType::GET;
        ss >> cmd.key;
    } 
    else if (cmd_name == "DEL") {
        cmd.type = CommandType::DEL;
        ss >> cmd.key;
    } 
    else {
        cmd.type = CommandType::UNKNOWN;
        cmd.valid = false;
    }
    return cmd;
}

