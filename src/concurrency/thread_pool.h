#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <functional>

class ThreadPool {
public:
    ThreadPool(size_t num_threads, std::function<void(int)> handler);
    void enqueue(int client_socket);
    ~ThreadPool();

private:
    std::vector<std::thread> workers_;
    std::queue<int> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
    std::function<void(int)> handler_;
    
    void worker_loop();
};

