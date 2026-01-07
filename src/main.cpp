#include "net/server.h"
#include "storage/kv_store.h"

int main() {
    KVStore store("../data/wal.log");
    Server server(8080, store);
    server.run();
    return 0;
}