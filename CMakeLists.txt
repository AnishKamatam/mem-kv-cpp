cmake_minimum_required(VERSION 3.10)

project(mem-kv-cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Crucial: This lets you do #include "net/server.h" from anywhere
include_directories(src)

file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")
list(REMOVE_ITEM ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/benchmark.cpp")

add_executable(mem-kv-server ${ALL_SOURCES})

# Benchmark tool
add_executable(benchmark src/tools/benchmark.cpp)

# Link threading library for our Concurrency phase
find_package(Threads REQUIRED)
target_link_libraries(mem-kv-server PRIVATE Threads::Threads)
target_link_libraries(benchmark PRIVATE Threads::Threads)