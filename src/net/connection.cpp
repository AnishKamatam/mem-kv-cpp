#include "connection.h"
#include "../protocol/parser.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

Connection::Connection(int sock_fd, KVStore& store, WriteBatcher& batcher) 
    : sock_fd_(sock_fd), store_(store), batcher_(batcher) {}

void Connection::handle() {
    char buffer[BUFFER_SIZE];
    while (true) {
        std::memset(buffer, 0, BUFFER_SIZE);
        
        ssize_t bytes = recv(sock_fd_, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            break;
        }
        
        std::string raw(buffer, bytes);
        
        ParsedCommand cmd = Parser::parse(raw);
        
        // Route writes through batcher, reads directly
        if (cmd.type == CommandType::SET || cmd.type == CommandType::DEL) {
            batcher_.add_to_batch(cmd);
            send(sock_fd_, "OK\n", 3, 0); // Immediate acknowledgment
        } else {
            // GET, MGET, STATS, etc. execute immediately
            std::string response = store_.execute(cmd);
            send(sock_fd_, response.c_str(), response.length(), 0);
        }
    }
    
    close(sock_fd_);
}

