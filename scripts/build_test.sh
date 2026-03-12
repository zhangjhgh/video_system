#!/bin/bash

echo "=== 视频处理系统环境测试 ==="

# 进入脚本所在目录，然后进入项目根目录
cd "$(dirname "${BASH_SOURCE[0]}")/.."
PROJECT_DIR="$(pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "项目目录: $PROJECT_DIR"
echo "构建目录: $BUILD_DIR"

# 清理并创建构建目录
echo "准备构建目录..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 运行 CMake
echo "运行 CMake..."
cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "❌ CMake 配置失败"
    exit 1
fi

echo "✅ CMake 配置成功"

# 构建项目
echo "构建项目..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "❌ 构建失败"
    exit 1
fi

echo "✅ 构建成功"

# 检查构建结果
echo "检查构建结果..."
if [ -f "bin/simple_test" ]; then
    echo "✅ simple_test 构建成功"
else
    echo "❌ simple_test 未找到"
fi

if [ -f "bin/test_opencv_qt" ]; then
    echo "✅ test_opencv_qt 构建成功"
else
    echo "❌ test_opencv_qt 未找到"
fi

# 运行简单测试
echo "运行简单测试 (OpenCV only)..."
if [ -f "bin/simple_test" ]; then
    cd bin
    ./simple_test
else
    echo "跳过 simple_test"
fi

# 运行集成测试
echo "运行集成测试 (OpenCV + Qt)..."
if [ -f "bin/test_opencv_qt" ]; then
    # 设置显示（如果需要）
    export DISPLAY=:0
    ./test_opencv_qt
else
    echo "跳过 test_opencv_qt"
fi

echo "=== 环境测试完成 ==="