# TODO: Add tests and install targets if needed.
cmake_minimum_required(VERSION 3.15)
project("PubSubBase")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable vcpkg manifest if not already integrated
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# Find the ZeroMQ and cppzmq package
find_package(cppzmq CONFIG REQUIRED)
find_package(spdlog REQUIRED)

# Add source to this project's executable.
add_executable (
    PubSubBase 
    "main.cpp" 
    "PubSubBase.hpp"
)

# Link cppzmq (this will link libzmq transitively)
target_link_libraries(PubSubBase PRIVATE cppzmq)
target_link_libraries(PubSubBase PRIVATE spdlog::spdlog)
