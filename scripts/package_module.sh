#!/bin/bash
# HyperPredict 模块打包脚本
# 支持 Magisk、APatch、KernelSU

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
MODULE_DIR="${PROJECT_ROOT}/magisk_module"
OUTPUT_DIR="${PROJECT_ROOT}/output"

# 读取版本号
if [ -f "${PROJECT_ROOT}/.version" ]; then
    VERSION=$(head -n 1 "${PROJECT_ROOT}/.version")
    VERSION_CODE=$(tail -n 1 "${PROJECT_ROOT}/.version")
else
    VERSION=$(grep "^version=" "${PROJECT_ROOT}/module.prop" | cut -d'=' -f2)
    VERSION_CODE=$(grep "^versionCode=" "${PROJECT_ROOT}/module.prop" | cut -d'=' -f2)
fi

# 获取 Git 信息
GIT_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

# 输出构建信息
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}HyperPredict 模块打包${NC}"
echo -e "${BLUE}========================================${NC}"
echo "版本: ${VERSION}"
echo "版本号: ${VERSION_CODE}"
echo "Git 提交: ${GIT_COMMIT}"
echo "Git 分支: ${GIT_BRANCH}"
echo "构建时间: ${BUILD_DATE}"
echo -e "${BLUE}========================================${NC}"

# 检查构建目录
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}错误: 构建目录不存在，请先运行构建${NC}"
    exit 1
fi

# 检查二进制文件
if [ ! -f "$BUILD_DIR/hyperpredictd" ]; then
    echo -e "${RED}错误: 二进制文件不存在${NC}"
    exit 1
fi

# 清理旧的模块目录
rm -rf "$MODULE_DIR"
mkdir -p "$MODULE_DIR"

# 创建模块目录结构
echo "创建模块目录结构..."
mkdir -p "$MODULE_DIR/system/bin"
mkdir -p "$MODULE_DIR/logs"
mkdir -p "$MODULE_DIR/webroot"
mkdir -p "$MODULE_DIR/META-INF/com/google/android"
mkdir -p "$MODULE_DIR/scripts"

# 复制二进制文件
echo "复制二进制文件..."
cp "$BUILD_DIR/hyperpredictd" "$MODULE_DIR/system/bin/"
chmod 755 "$MODULE_DIR/system/bin/hyperpredictd"

# 复制 WebUI 文件
if [ -d "${PROJECT_ROOT}/webroot" ]; then
    echo "复制 WebUI 文件..."
    cp -r "${PROJECT_ROOT}/webroot"/* "$MODULE_DIR/webroot/"
    chmod -R 644 "$MODULE_DIR/webroot"/*
fi

# 复制脚本文件
echo "复制脚本文件..."
cp "${PROJECT_ROOT}/scripts/install_module.sh" "$MODULE_DIR/scripts/"
cp "${PROJECT_ROOT}/scripts/uninstall_module.sh" "$MODULE_DIR/scripts/"
cp "${PROJECT_ROOT}/scripts/service.sh" "$MODULE_DIR/service.sh"
chmod 755 "$MODULE_DIR/scripts/"*.sh
chmod 755 "$MODULE_DIR/service.sh"

# 创建 module.prop
echo "创建 module.prop..."
cat > "$MODULE_DIR/module.prop" << EOF
id=hyperpredict
name=HyperPredict
version=${VERSION}
versionCode=${VERSION_CODE}
author=ye3912
description=AI CPU Scheduler with KSU WebUI Support
updateJson=https://raw.githubusercontent.com/ye3912/HyperPredict/main/update.json
webui=webroot
support=Magisk,APatch,KernelSU
EOF

# 创建 service.sh
echo "创建 service.sh..."
cat > "$MODULE_DIR/service.sh" << 'EOF'
#!/system/bin/sh
# HyperPredict 服务启动脚本

# 等待系统启动完成
sleep 15

# 设置环境变量
MODDIR=${0%/*}
LOGFILE="$MODDIR/logs/hp.log"
PIDFILE="$MODDIR/logs/hp.pid"

# 检查是否已经在运行
if [ -f "$PIDFILE" ]; then
    OLD_PID=$(cat "$PIDFILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "HyperPredict 已在运行 (PID: $OLD_PID)"
        exit 0
    fi
fi

# 停止旧进程
pkill -9 hyperpredictd 2>/dev/null || true

# 启动守护进程
echo "启动 HyperPredict..."
nohup "$MODDIR/system/bin/hyperpredictd" > "$LOGFILE" 2>&1 &
PID=$!
echo "$PID" > "$PIDFILE"

echo "HyperPredict 已启动 (PID: $PID)"
EOF

chmod 755 "$MODULE_DIR/service.sh"

# 创建 uninstall.sh
echo "创建 uninstall.sh..."
cat > "$MODULE_DIR/uninstall.sh" << 'EOF'
#!/system/bin/sh
# HyperPredict 卸载脚本

MODDIR=${0%/*}

# 停止守护进程
if [ -f "$MODDIR/logs/hp.pid" ]; then
    PID=$(cat "$MODDIR/logs/hp.pid")
    kill "$PID" 2>/dev/null || true
    rm -f "$MODDIR/logs/hp.pid"
fi

pkill -9 hyperpredictd 2>/dev/null || true

# 清理文件
rm -rf "$MODDIR"

echo "HyperPredict 已卸载"
EOF

chmod 755 "$MODULE_DIR/uninstall.sh"

# 创建 update-binary (Magisk 兼容)
echo "创建 update-binary..."
cat > "$MODULE_DIR/META-INF/com/google/android/update-binary" << 'EOF'
#!/sbin/sh
# Magisk Module Installer Script

OUTFD=$2
ZIPFILE=$3

mount /data 2>/dev/null || true

# 检测模块管理器
if [ -n "$KSU" ]; then
    echo "KernelSU detected"
elif [ -n "$APATCH" ]; then
    echo "APatch detected"
elif [ -n "$MAGISK_VER" ]; then
    echo "Magisk detected"
else
    echo "Unknown module manager"
fi

# 解压模块
unzip -o "$ZIPFILE" -d /data/adb/modules/hyperpredict

# 设置权限
chmod 755 /data/adb/modules/hyperpredict/system/bin/hyperpredictd
chmod 755 /data/adb/modules/hyperpredict/service.sh
chmod 755 /data/adb/modules/hyperpredict/uninstall.sh

echo "HyperPredict 安装完成"
EOF

chmod 755 "$MODULE_DIR/META-INF/com/google/android/update-binary"

# 创建 updater-script
echo "创建 updater-script..."
cat > "$MODULE_DIR/META-INF/com/google/android/updater-script" << 'EOF'
#MAGISK
EOF

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 打包模块
echo "打包模块..."
cd "$MODULE_DIR"
ZIP_NAME="HyperPredict-${VERSION}-${GIT_COMMIT}.zip"
zip -r "${OUTPUT_DIR}/${ZIP_NAME}" .

# 创建符号链接（最新版本）
cd "$OUTPUT_DIR"
rm -f HyperPredict-latest.zip
ln -s "$ZIP_NAME" HyperPredict-latest.zip

# 输出结果
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}打包完成${NC}"
echo -e "${GREEN}========================================${NC}"
echo "输出文件: ${OUTPUT_DIR}/${ZIP_NAME}"
echo "符号链接: ${OUTPUT_DIR}/HyperPredict-latest.zip"
echo "文件大小: $(du -h "${OUTPUT_DIR}/${ZIP_NAME}" | cut -f1)"
echo -e "${GREEN}========================================${NC}"

# 计算文件哈希
if command -v sha256sum &> /dev/null; then
    SHA256=$(sha256sum "${OUTPUT_DIR}/${ZIP_NAME}" | cut -d' ' -f1)
    echo "SHA256: ${SHA256}"
    echo "${SHA256}" > "${OUTPUT_DIR}/${ZIP_NAME}.sha256"
fi

exit 0
