#include "server.h"
#include "connection.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>

Server::Server(int port, KVStore& store, size_t num_threads) 
    : port_(port), server_fd_(-1), store_(store) {
    thread_pool_ = std::make_unique<ThreadPool>(num_threads, [this](int client_socket) {
        Connection conn(client_socket, store_);
        conn.handle();
    });
}

void Server::run() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        perror("Socket failed");
        return;
    }

    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_fd_);
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd_);
        return;
    }

    if (listen(server_fd_, SOMAXCONN) < 0) {
        perror("Listen failed");
        close(server_fd_);
        return;
    }

    std::cout << "Server listening on port " << port_ << "..." << std::endl;

    while (true) {
        sockaddr_in address;
        socklen_t addrlen = sizeof(address);
        
        int client_socket = accept(server_fd_, reinterpret_cast<sockaddr*>(&address), &addrlen);
        
        if (client_socket >= 0) {
            thread_pool_->enqueue(client_socket);
        } else {
            perror("Accept failed");
        }
    }

    close(server_fd_);
}

