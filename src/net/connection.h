#pragma once

#include "../storage/kv_store.h"
#include "../batching/write_batcher.h"
#include <string>

class Connection {
public:
    Connection(int sock_fd, KVStore& store, WriteBatcher& batcher);
    void handle();

private:
    int sock_fd_;
    KVStore& store_;
    WriteBatcher& batcher_;
    static constexpr int BUFFER_SIZE = 1024;
};

