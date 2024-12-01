
find_package(spdlog REQUIRED)

# Add source to this project's executable.
add_executable (
    PubSubBase 
    "main.cpp"
    "count_msg.h" 
    "status_msg.h"
)

# Add the parent directory and this one to the include path
target_include_directories(PubSubBase PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(PubSubBase PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(PubSubBase PRIVATE spdlog::spdlog)