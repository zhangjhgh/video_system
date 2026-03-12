#!/bin/bash

echo "=== CMake调试 ==="
cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "当前目录: $(pwd)"
echo "目录内容:"
ls -la

echo "CMake版本:"
cmake --version

echo "OpenCV信息:"
pkg-config --cflags --libs opencv4

echo "Qt信息:"
qmake --version

echo "运行CMake详细输出..."
mkdir -p build_debug
cd build_debug
cmake .. --debug-output

echo "CMake缓存内容:"
cat CMakeCache.txt | grep -E "(OpenCV|Qt)"