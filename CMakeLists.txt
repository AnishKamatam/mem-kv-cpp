# 1. Set the minimum version of CMake required
cmake_minimum_required(VERSION 3.10)

# 2. Name your project
project(mem-kv-cpp)

# 3. Tell CMake to use modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. Define the executable and the files needed to build it
# This creates an app called 'mem-kv-server' from 'main.cpp'
add_executable(mem-kv-server src/main.cpp)

# 5. Link necessary libraries (like threads for your concurrency phase)
find_package(Threads REQUIRED)
target_link_libraries(mem-kv-server PRIVATE Threads::Threads)