#!/bin/bash

echo "=== 分步环境测试 ==="
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# 步骤1: 检查基础环境
echo "步骤1: 检查基础环境..."
g++ --version && echo "✅ g++ 正常" || echo "❌ g++ 异常"
cmake --version && echo "✅ CMake 正常" || echo "❌ CMake 异常"
pkg-config --modversion opencv4 && echo "✅ OpenCV 正常" || echo "❌ OpenCV 异常"
qmake --version && echo "✅ qmake 正常" || echo "❌ qmake 异常"

# 步骤2: 创建构建目录
echo "步骤2: 准备构建环境..."
BUILD_DIR="build_step_test"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 步骤3: 运行 CMake
echo "步骤3: 运行 CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "❌ CMake 失败"
    exit 1
fi
echo "✅ CMake 成功"

# 步骤4: 构建 simple_test
echo "步骤4: 构建 simple_test (仅 OpenCV)..."
make simple_test
if [ $? -eq 0 ] && [ -f "bin/simple_test" ]; then
    echo "✅ simple_test 构建成功"
    echo "运行 simple_test..."
    cd bin
    ./simple_test
    cd ..
else
    echo "❌ simple_test 构建失败"
fi

# 步骤5: 构建 test_opencv_qt
echo "步骤5: 构建 test_opencv_qt (OpenCV + Qt)..."
make test_opencv_qt
if [ $? -eq 0 ] && [ -f "bin/test_opencv_qt" ]; then
    echo "✅ test_opencv_qt 构建成功"
    echo "注意: 如果运行在无显示环境，GUI测试可能会失败"
else
    echo "❌ test_opencv_qt 构建失败"
fi

echo "=== 分步测试完成 ==="