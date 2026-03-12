#!/bin/bash

echo "=== 环境检查 ==="

# 检查编译器
echo "1. 检查编译器..."
g++ --version | head -1
if [ $? -eq 0 ]; then
    echo "✅ g++ 可用"
else
    echo "❌ g++ 不可用"
    exit 1
fi

# 检查Qt
echo "2. 检查Qt..."
qmake --version | head -1
if [ $? -eq 0 ]; then
    echo "✅ qmake 可用"
else
    echo "❌ qmake 不可用"
    exit 1
fi

# 检查OpenCV
echo "3. 检查OpenCV..."
pkg-config --modversion opencv4
if [ $? -eq 0 ]; then
    echo "✅ OpenCV 可用"
else
    echo "❌ OpenCV 不可用"
    exit 1
fi

# 检查CMake
echo "4. 检查CMake..."
cmake --version | head -1
if [ $? -eq 0 ]; then
    echo "✅ CMake 可用"
else
    echo "❌ CMake 不可用"
    exit 1
fi

echo "=== 所有环境依赖检查通过 ==="