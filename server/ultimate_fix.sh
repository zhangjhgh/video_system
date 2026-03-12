#!/bin/bash

echo "=== 视频处理服务器终极修复 ==="
cd /home/zhangsan/~projects/video_system/server

# 1. 完全删除旧文件并创建全新文件
echo "1. 创建全新的 CMakeLists.txt..."
rm -f CMakeLists.txt

# 使用 here document 创建全新文件，避免复制粘贴问题
cat > CMakeLists.txt << 'CMAKE_EOF'
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
CMAKE_EOF

# 2. 验证文件创建成功
echo "2. 验证文件..."
if [ ! -f "CMakeLists.txt" ]; then
    echo "❌ CMakeLists.txt 创建失败"
    exit 1
fi

echo "文件大小: $(wc -l < CMakeLists.txt) 行"
echo "文件内容预览:"
head -5 CMakeLists.txt

# 3. 清理并构建
echo ""
echo "3. 清理构建目录..."
rm -rf build
mkdir build
cd build

echo "4. 运行 CMake..."
if cmake .. > cmake.log 2>&1; then
    echo "✅ CMake 配置成功"
    echo "CMake 输出摘要:"
    grep -E "(OpenCV|found|Configuring|Generating)" cmake.log
    
    echo ""
    echo "5. 编译项目..."
    if make -j4 > make.log 2>&1; then
        echo "✅ 编译成功!"
        echo ""
        if [ -f "bin/video_server" ]; then
            echo "🎉 成功创建可执行文件: bin/video_server"
            echo ""
            echo "文件信息:"
            file bin/video_server
            echo ""
            echo "运行测试: ./bin/video_server"
        else
            echo "⚠️  可执行文件不在预期位置，查找中..."
            find . -name "video_server" -type f
        fi
    else
        echo "❌ 编译失败"
        echo "编译错误摘要:"
        grep -i error make.log | head -10
        echo ""
        echo "查看完整日志: make.log"
    fi
else
    echo "❌ CMake 配置失败"
    echo "CMake 错误摘要:"
    grep -i error cmake.log | head -10
    echo ""
    echo "查看完整日志: cmake.log"
fi
