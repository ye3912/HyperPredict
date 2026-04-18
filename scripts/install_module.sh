#!/system/bin/sh
# HyperPredict 通用安装脚本
# 支持 Magisk、APatch、KernelSU

# 检测 Root 环境
if [ "$(id -u)" -ne 0 ]; then
    echo "错误: 需要 root 权限"
    exit 1
fi

# 检测模块管理器类型
detect_module_manager() {
    if [ -n "$KSU" ]; then
        echo "ksu"
    elif [ -n "$APATCH" ]; then
        echo "apatch"
    elif [ -n "$MAGISK_VER" ]; then
        echo "magisk"
    else
        # 尝试通过文件系统检测
        if [ -d "/data/adb/ksu" ]; then
            echo "ksu"
        elif [ -d "/data/adb/ap" ]; then
            echo "apatch"
        elif [ -d "/data/adb/magisk" ]; then
            echo "magisk"
        else
            echo "unknown"
        fi
    fi
}

MODULE_MANAGER=$(detect_module_manager)
echo "检测到模块管理器: $MODULE_MANAGER"

# 设置模块路径
if [ "$MODULE_MANAGER" = "ksu" ]; then
    MODPATH="/data/adb/modules/hyperpredict"
elif [ "$MODULE_MANAGER" = "apatch" ]; then
    MODPATH="/data/adb/modules/hyperpredict"
elif [ "$MODULE_MANAGER" = "magisk" ]; then
    MODPATH="/data/adb/modules/hyperpredict"
else
    MODPATH="/data/adb/modules/hyperpredict"
fi

# 创建模块目录
mkdir -p "$MODPATH"
mkdir -p "$MODPATH/system/bin"
mkdir -p "$MODPATH/logs"
mkdir -p "$MODPATH/webroot"

# 复制文件
echo "安装 HyperPredict..."
cp -f "$MODPATH/system/bin/hyperpredictd" "$MODPATH/system/bin/hyperpredictd" 2>/dev/null || true
chmod 755 "$MODPATH/system/bin/hyperpredictd"

# 复制 WebUI 文件
if [ -d "$MODPATH/webroot" ]; then
    echo "安装 WebUI..."
    cp -rf "$MODPATH/webroot"/* "$MODPATH/webroot/" 2>/dev/null || true
fi

# 创建 service.sh
cat > "$MODPATH/service.sh" << 'EOF'
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

chmod 755 "$MODPATH/service.sh"

# 创建卸载脚本
cat > "$MODPATH/uninstall.sh" << 'EOF'
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

chmod 755 "$MODPATH/uninstall.sh"

# 设置权限
chmod -R 755 "$MODPATH/system/bin"
chmod -R 644 "$MODPATH/webroot" 2>/dev/null || true

# 创建日志目录
mkdir -p "$MODPATH/logs"
chmod 755 "$MODPATH/logs"

# 输出安装信息
echo "=========================================="
echo "HyperPredict 安装完成"
echo "=========================================="
echo "模块路径: $MODPATH"
echo "二进制文件: $MODPATH/system/bin/hyperpredictd"
echo "日志文件: $MODPATH/logs/hp.log"
echo "WebUI: http://localhost:8081"
echo "=========================================="

# 重启服务（如果需要）
if [ "$MODULE_MANAGER" = "magisk" ]; then
    # Magisk 会自动重启服务
    echo "Magisk 将自动重启服务"
else
    # KSU/APatch 需要手动重启
    echo "请重启设备以激活模块"
fi

exit 0
