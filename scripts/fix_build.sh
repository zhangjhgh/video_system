#!/bin/bash

echo "=== 修复构建问题 ==="
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# 清理构建目录
echo "清理构建目录..."
rm -rf build build_step_test build_robust

# 创建新的构建目录
BUILD_DIR="build_fixed"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 运行 CMake
echo "运行 CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "❌ CMake 配置失败"
    exit 1
fi

echo "✅ CMake 配置成功"

# 分别构建每个目标
echo "构建 simple_test..."
make simple_test
if [ $? -eq 0 ] && [ -f "bin/simple_test" ]; then
    echo "✅ simple_test 构建成功"
else
    echo "❌ simple_test 构建失败"
    exit 1
fi

echo "构建 test_opencv_qt..."
make test_opencv_qt
if [ $? -eq 0 ] && [ -f "bin/test_opencv_qt" ]; then
    echo "✅ test_opencv_qt 构建成功"
else
    echo "❌ test_opencv_qt 构建失败"
    exit 1
fi

# 运行测试
echo "运行测试..."
echo "=== 运行 simple_test ==="
cd bin
./simple_test

echo "=== 运行 test_opencv_qt ==="
export DISPLAY=:0
./test_opencv_qt

echo "=== 构建修复完成 ==="