#include "parser.h"
#include <sstream>
#include <cstring>
#include <vector>

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
        
        // Read value (may contain spaces)
        std::string value_part;
        std::getline(ss >> std::ws, value_part);
        
        // Check for TTL: SET key value EX 3600
        std::stringstream value_stream(value_part);
        std::string ttl_keyword;
        std::string potential_ttl;
        
        // Try to parse TTL if present
        std::vector<std::string> parts;
        std::string part;
        while (value_stream >> part) {
            parts.push_back(part);
        }
        
        if (parts.size() >= 3 && (parts[parts.size()-2] == "EX" || parts[parts.size()-2] == "TTL")) {
            // Has TTL: value is everything except last 2 parts
            cmd.ttl_seconds = std::stoi(parts.back());
            for (size_t i = 0; i < parts.size() - 2; ++i) {
                if (i > 0) cmd.value += " ";
                cmd.value += parts[i];
            }
        } else {
            // No TTL: entire value_part is the value
            cmd.value = value_part;
        }
    } 
    else if (cmd_name == "GET") {
        cmd.type = CommandType::GET;
        ss >> cmd.key;
    } 
    else if (cmd_name == "DEL") {
        cmd.type = CommandType::DEL;
        ss >> cmd.key;
    }
    else if (cmd_name == "COMPACT") {
        cmd.type = CommandType::COMPACT;
    }
    else if (cmd_name == "STATS") {
        cmd.type = CommandType::STATS;
    }
    else if (cmd_name == "MGET") {
        cmd.type = CommandType::MGET;
        std::string key;
        while (ss >> key) {
            cmd.keys.push_back(key);
        }
        cmd.valid = !cmd.keys.empty(); // Valid only if we have at least one key
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
    else if (cmd_name == "COMPACT" && array_len == 1) {
        cmd.type = CommandType::COMPACT;
        cmd.valid = true;
    }
    else if (cmd_name == "MGET" && array_len >= 2) {
        cmd.type = CommandType::MGET;
        
        for (int i = 1; i < array_len; ++i) {
            std::getline(ss, line);
            if (line[0] != '$') {
                cmd.valid = false;
                return cmd;
            }
            int key_len = std::stoi(line.substr(1));
            std::string key;
            key.resize(key_len);
            ss.read(&key[0], key_len);
            std::getline(ss, line);
            cmd.keys.push_back(key);
        }
        cmd.valid = true;
    }
    else {
        cmd.type = CommandType::UNKNOWN;
        cmd.valid = false;
    }
    
    return cmd;
}

