#!/system/bin/sh
# HyperPredict 服务启动脚本
# 支持 Magisk、APatch、KernelSU

# 等待系统启动完成
sleep 15

# 设置环境变量
MODDIR=${0%/*}
LOGFILE="$MODDIR/logs/hp.log"
PIDFILE="$MODDIR/logs/hp.pid"

# 检查模块目录是否存在
if [ ! -d "$MODDIR" ]; then
    echo "错误: 模块目录不存在: $MODDIR"
    exit 1
fi

# 检查二进制文件是否存在
if [ ! -f "$MODDIR/system/bin/hyperpredictd" ]; then
    echo "错误: 二进制文件不存在: $MODDIR/system/bin/hyperpredictd"
    exit 1
fi

# 创建日志目录
mkdir -p "$MODDIR/logs"

# 检查是否已经在运行
if [ -f "$PIDFILE" ]; then
    OLD_PID=$(cat "$PIDFILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "HyperPredict 已在运行 (PID: $OLD_PID)"
        exit 0
    else
        echo "清理旧的 PID 文件"
        rm -f "$PIDFILE"
    fi
fi

# 停止旧进程
pkill -9 hyperpredictd 2>/dev/null || true

# 等待进程完全停止
sleep 2

# 启动守护进程
echo "启动 HyperPredict..."
echo "模块目录: $MODDIR"
echo "日志文件: $LOGFILE"
echo "二进制文件: $MODDIR/system/bin/hyperpredictd"

nohup "$MODDIR/system/bin/hyperpredictd" > "$LOGFILE" 2>&1 &
PID=$!

# 保存 PID
echo "$PID" > "$PIDFILE"

# 检查进程是否启动成功
sleep 1
if kill -0 "$PID" 2>/dev/null; then
    echo "HyperPredict 已启动 (PID: $PID)"
else
    echo "错误: HyperPredict 启动失败"
    rm -f "$PIDFILE"
    exit 1
fi

# 输出启动信息
echo "=========================================="
echo "HyperPredict 服务已启动"
echo "=========================================="
echo "PID: $PID"
echo "日志: $LOGFILE"
echo "WebUI: http://localhost:8081"
echo "=========================================="

exit 0
