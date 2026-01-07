#pragma once

#include "../storage/kv_store.h"
#include "../concurrency/thread_pool.h"
#include <memory>

class Server {
public:
    Server(int port, KVStore& store, size_t num_threads = 8);
    void run();

private:
    int server_fd_;
    int port_;
    KVStore& store_;
    std::unique_ptr<ThreadPool> thread_pool_;
};

