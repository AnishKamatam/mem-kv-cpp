#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 1024;
constexpr int BACKLOG = 3;

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
                // bytes_received == 0 means client disconnected gracefully
                std::cout << "Client disconnected." << std::endl;
                break;
            }

            std::cout << "Client sent: " << buffer;

            std::string response = "OK\n";
            send(client_socket, response.c_str(), response.length(), 0);
        }

        close(client_socket);
    }

    close(server_fd);
    return 0;
}