#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

void run_client(int client_id, int requests) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to connect" << std::endl;
        close(sock);
        return;
    }
    
    char buffer[1024];
    for (int i = 0; i < requests; ++i) {
        std::string cmd = "SET key_" + std::to_string(client_id) + "_" + std::to_string(i) + " value_" + std::to_string(i) + "\n";
        
        send(sock, cmd.c_str(), cmd.length(), 0);
        
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            break;
        }
    }
    
    close(sock);
}

int main(int argc, char* argv[]) {
    int concurrent_clients = 10;
    int requests_per_client = 1000;
    
    if (argc >= 2) {
        concurrent_clients = std::stoi(argv[1]);
    }
    if (argc >= 3) {
        requests_per_client = std::stoi(argv[2]);
    }
    
    std::cout << "Starting benchmark: " << concurrent_clients 
              << " clients, " << requests_per_client << " requests each..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < concurrent_clients; ++i) {
        threads.emplace_back(run_client, i, requests_per_client);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    
    int total_reqs = concurrent_clients * requests_per_client;
    double rps = total_reqs / diff.count();
    
    std::cout << "------------------------------" << std::endl;
    std::cout << "Total Requests: " << total_reqs << std::endl;
    std::cout << "Total Time:     " << diff.count() << " s" << std::endl;
    std::cout << "Requests/sec:   " << static_cast<int>(rps) << std::endl;
    std::cout << "------------------------------" << std::endl;
    
    return 0;
}

