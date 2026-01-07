#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 1024;
constexpr int BACKLOG = 3;

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket failed");
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

    socklen_t addrlen = sizeof(address);
    int client_socket = accept(server_fd, reinterpret_cast<sockaddr*>(&address), &addrlen);
    if (client_socket < 0) {
        perror("Accept failed");
        close(server_fd);
        return 1;
    }

    char buffer[BUFFER_SIZE] = {0};
    ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read < 0) {
        perror("Read failed");
        close(client_socket);
        close(server_fd);
        return 1;
    }

    std::cout << "Received: " << buffer << std::endl;

    close(client_socket);
    close(server_fd);
    return 0;
}