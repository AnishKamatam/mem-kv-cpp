cmake_minimum_required(VERSION 3.10)

project(mem-kv-cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Crucial: This lets you do #include "net/server.h" from anywhere
include_directories(src)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(mem-kv-server ${SOURCES})

# Link threading library for our Concurrency phase
find_package(Threads REQUIRED)
target_link_libraries(mem-kv-server PRIVATE Threads::Threads)