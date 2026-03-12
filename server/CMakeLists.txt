cmake_minimum_required(VERSION 3.16)
project(VideoProcessingServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
endif()

# Find dependencies
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# Include directories
include_directories(include)
include_directories(${OpenCV_INCLUDE_DIRS})

# Source files - use recursive glob to find all cpp files
file(GLOB_RECURSE SERVER_SOURCES
    "src/*.cpp"
    "src/*/*.cpp"
)

# Create executable
add_executable(video_server ${SERVER_SOURCES})

# Link libraries
target_link_libraries(video_server
    ${OpenCV_LIBS}
    pthread
    ${CMAKE_THREAD_LIBS_INIT}
)

# Output directory
set_target_properties(video_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Create output directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
