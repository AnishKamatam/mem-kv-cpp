#pragma once

#include "../storage/kv_store.h"

class Server {
public:
    Server(int port, KVStore& store);
    void run();

private:
    int server_fd_;
    int port_;
    KVStore& store_;
    
    static constexpr int BACKLOG = 3;
};

