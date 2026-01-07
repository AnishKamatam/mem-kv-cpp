#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 1024;
constexpr int BACKLOG = 3;

// Our "Database"
std::unordered_map<std::string, std::string> kv_store;

enum class CommandType { SET, GET, DEL, UNKNOWN };

struct ParsedCommand {
    CommandType type;
    std::string key;
    std::string value;
};

ParsedCommand parse_command(const std::string& raw_input) {
    std::stringstream ss(raw_input);
    std::string cmd_part, key, val;
    
    ss >> cmd_part;
    
    ParsedCommand cmd;
    if (cmd_part == "SET") {
        cmd.type = CommandType::SET;
        ss >> cmd.key;
        // Use getline to handle values with spaces
        std::getline(ss >> std::ws, cmd.value); 
    } 
    else if (cmd_part == "GET") {
        cmd.type = CommandType::GET;
        ss >> cmd.key;
    } 
    else if (cmd_part == "DEL") {
        cmd.type = CommandType::DEL;
        ss >> cmd.key;
    } 
    else {
        cmd.type = CommandType::UNKNOWN;
    }
    return cmd;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket failed");
        return 1;
    }

    // Allow port reuse to avoid "Address already in use" errors
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_fd);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        socklen_t addrlen = sizeof(address);
        int client_socket = accept(server_fd, reinterpret_cast<sockaddr*>(&address), &addrlen);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        std::cout << "Client connected!" << std::endl;

        char buffer[BUFFER_SIZE];
        while (true) {
            std::memset(buffer, 0, BUFFER_SIZE);
            
            ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_received <= 0) {
                // bytes_received == 0 indicates graceful disconnect
                std::cout << "Client disconnected." << std::endl;
                break;
            }

            std::cout << "Client sent: " << buffer;

            std::string input(buffer);
            ParsedCommand cmd = parse_command(input);
            
            std::string response;
            if (cmd.type == CommandType::SET) {
                kv_store[cmd.key] = cmd.value;
                response = "OK\n";
            } 
            else if (cmd.type == CommandType::GET) {
                if (kv_store.count(cmd.key)) {
                    response = kv_store[cmd.key] + "\n";
                } else {
                    response = "(nil)\n";
                }
            } 
            else if (cmd.type == CommandType::DEL) {
                kv_store.erase(cmd.key);
                response = "OK\n";
            } 
            else {
                response = "ERROR: Unknown command\n";
            }
            
            send(client_socket, response.c_str(), response.length(), 0);
        }

        close(client_socket);
    }

    close(server_fd);
    return 0;
}