#!/bin/bash

echo "=== 修复 Qt6 依赖问题 ==="

echo "1. 安装 OpenGL 开发包..."
sudo apt update
sudo apt install -y libgl1-mesa-dev libglu1-mesa-dev freeglut3-dev

echo "2. 安装 Qt6 完整开发包..."
sudo apt install -y qt6-base-dev qt6-tools-dev

echo "3. 验证安装..."
echo "检查 OpenGL:"
pkg-config --exists gl && echo "✅ OpenGL 已安装" || echo "❌ OpenGL 未安装"
pkg-config --exists glu && echo "✅ GLU 已安装" || echo "❌ GLU 未安装"

echo "检查 Qt6:"
qmake --version && echo "✅ qmake 可用" || echo "❌ qmake 不可用"

echo "4. 测试 CMake 配置..."
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p test_fix
cd test_fix
cmake .. > cmake_output.log 2>&1

if grep -q "Configuring done" cmake_output.log; then
    echo "✅ CMake 配置成功"
    echo "构建测试..."
    make -j4 > build_output.log 2>&1
    if [ $? -eq 0 ]; then
        echo "✅ 构建成功"
    else
        echo "❌ 构建失败，查看 build_output.log 获取详情"
    fi
else
    echo "❌ CMake 配置失败，查看 cmake_output.log 获取详情"
fi

echo "=== 依赖修复完成 ==="