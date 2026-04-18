#!/bin/bash
# HyperPredict 本地构建脚本
# 用于本地开发和测试

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
OUTPUT_DIR="${PROJECT_ROOT}/output"

# 检查 NDK
if [ -z "$ANDROID_NDK_HOME" ] && [ -z "$ANDROID_NDK_ROOT" ]; then
    echo -e "${RED}错误: 未设置 ANDROID_NDK_HOME 或 ANDROID_NDK_ROOT 环境变量${NC}"
    echo "请设置 Android NDK 路径，例如:"
    echo "export ANDROID_NDK_HOME=/path/to/ndk"
    exit 1
fi

NDK_PATH="${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}"
echo "使用 NDK: $NDK_PATH"

# 清理旧的构建目录
echo "清理旧的构建目录..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# 构建
echo "开始构建..."
cd "$BUILD_DIR"
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 \
    -DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF \
    -DCMAKE_CXX_FLAGS="-std=c++20 -Wall -Wextra -Werror=format-security -fno-exceptions -fno-rtti -ffast-math -funwind-tables -fstack-protector-strong" \
    -GNinja

ninja

# 检查构建结果
if [ ! -f "$BUILD_DIR/hyperpredictd" ]; then
    echo -e "${RED}构建失败: 二进制文件不存在${NC}"
    exit 1
fi

echo -e "${GREEN}构建成功${NC}"
echo "二进制文件: $BUILD_DIR/hyperpredictd"
echo "文件大小: $(du -h "$BUILD_DIR/hyperpredictd" | cut -f1)"

# 询问是否打包
echo ""
read -p "是否打包模块? (y/n) " -n 1 -r
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    chmod +x "${PROJECT_ROOT}/scripts/package_module.sh"
    "${PROJECT_ROOT}/scripts/package_module.sh"
fi

exit 0
