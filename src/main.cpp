#include "net/server.h"
#include "storage/kv_store.h"
#include <thread>

int main() {
    KVStore store("../data/wal.log");
    
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
        num_threads = 8;
    }
    
    Server server(8080, store, num_threads);
    server.run();
    return 0;
}