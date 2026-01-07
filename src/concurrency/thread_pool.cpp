#include "thread_pool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads, std::function<void(int)> handler)
    : stop_(false), handler_(handler) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

void ThreadPool::worker_loop() {
    while (true) {
        int socket;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { 
                return stop_ || !tasks_.empty(); 
            });
            
            if (stop_ && tasks_.empty()) {
                return;
            }
            
            socket = tasks_.front();
            tasks_.pop();
        }
        
        handler_(socket);
    }
}

void ThreadPool::enqueue(int client_socket) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            return;
        }
        tasks_.push(client_socket);
    }
    condition_.notify_one();
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

