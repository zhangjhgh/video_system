#!/bin/bash

echo "快速构建测试..."

# 直接在video_system目录中构建
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p build
cd build

echo "当前目录: $(pwd)"
echo "运行CMake..."
cmake ..

if [ $? -eq 0 ]; then
    echo "编译..."
    make -j4
    if [ $? -eq 0 ]; then
        echo "✅ 构建成功！"
        if [ -f "tests/test_opencv_qt" ]; then
            echo "运行测试..."
            ./tests/test_opencv_qt
        else
            echo "❌ 可执行文件未找到"
        fi
    else
        echo "❌ 编译失败"
    fi
else
    echo "❌ CMake配置失败"
fi