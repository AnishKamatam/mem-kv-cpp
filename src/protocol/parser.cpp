#include "parser.h"
#include <sstream>
#include <cstring>

ParsedCommand Parser::parse(const std::string& input) {
    if (input.empty()) {
        ParsedCommand cmd;
        cmd.type = CommandType::UNKNOWN;
        cmd.valid = false;
        return cmd;
    }
    
    if (input[0] == '*') {
        return parse_resp(input);
    }
    
    return parse_plain_text(input);
}

ParsedCommand Parser::parse_plain_text(const std::string& input) {
    std::stringstream ss(input);
    std::string cmd_name;
    ss >> cmd_name;
    
    ParsedCommand cmd;
    if (cmd_name == "SET") {
        cmd.type = CommandType::SET;
        ss >> cmd.key;
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

ParsedCommand Parser::parse_resp(const std::string& input) {
    ParsedCommand cmd;
    cmd.valid = false;
    
    std::istringstream ss(input);
    std::string line;
    
    if (!std::getline(ss, line) || line[0] != '*') {
        cmd.type = CommandType::UNKNOWN;
        return cmd;
    }
    
    int array_len = std::stoi(line.substr(1));
    if (array_len < 1) {
        cmd.type = CommandType::UNKNOWN;
        return cmd;
    }
    
    if (!std::getline(ss, line) || line[0] != '$') {
        cmd.type = CommandType::UNKNOWN;
        return cmd;
    }
    
    int cmd_len = std::stoi(line.substr(1));
    std::string cmd_name;
    cmd_name.resize(cmd_len);
    ss.read(&cmd_name[0], cmd_len);
    std::getline(ss, line);
    
    if (cmd_name == "SET" && array_len >= 3) {
        cmd.type = CommandType::SET;
        
        std::getline(ss, line);
        int key_len = std::stoi(line.substr(1));
        cmd.key.resize(key_len);
        ss.read(&cmd.key[0], key_len);
        std::getline(ss, line);
        
        std::getline(ss, line);
        int val_len = std::stoi(line.substr(1));
        cmd.value.resize(val_len);
        ss.read(&cmd.value[0], val_len);
        cmd.valid = true;
    }
    else if (cmd_name == "GET" && array_len >= 2) {
        cmd.type = CommandType::GET;
        
        std::getline(ss, line);
        int key_len = std::stoi(line.substr(1));
        cmd.key.resize(key_len);
        ss.read(&cmd.key[0], key_len);
        cmd.valid = true;
    }
    else if (cmd_name == "DEL" && array_len >= 2) {
        cmd.type = CommandType::DEL;
        
        std::getline(ss, line);
        int key_len = std::stoi(line.substr(1));
        cmd.key.resize(key_len);
        ss.read(&cmd.key[0], key_len);
        cmd.valid = true;
    }
    else {
        cmd.type = CommandType::UNKNOWN;
        cmd.valid = false;
    }
    
    return cmd;
}

