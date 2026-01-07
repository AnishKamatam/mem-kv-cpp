#pragma once

#include "../storage/kv_store.h"
#include <string>

class Connection {
public:
    Connection(int sock_fd, KVStore& store);
    void handle();

private:
    int sock_fd_;
    KVStore& store_;
    static constexpr int BUFFER_SIZE = 1024;
};

