#include "connection.h"
#include "../protocol/parser.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

Connection::Connection(int sock_fd, KVStore& store) 
    : sock_fd_(sock_fd), store_(store) {}

void Connection::handle() {
    char buffer[BUFFER_SIZE];
    while (true) {
        std::memset(buffer, 0, BUFFER_SIZE);
        
        ssize_t bytes = recv(sock_fd_, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            // bytes_received == 0 indicates graceful disconnect
            std::cout << "Client disconnected." << std::endl;
            break;
        }
        
        std::string raw(buffer, bytes);
        std::cout << "Client sent: " << raw;
        
        ParsedCommand cmd = Parser::parse(raw);
        std::string response = store_.execute(cmd);
        
        send(sock_fd_, response.c_str(), response.length(), 0);
    }
    
    close(sock_fd_);
}

