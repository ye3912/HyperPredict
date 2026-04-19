#!/system/bin/sh
# HyperPredict 服务启动脚本
# 符合 Magisk 模块规范

# 等待系统启动完成
sleep 15

# 设置环境变量
MODDIR=${0%/*}
LOGFILE="$MODDIR/logs/hp.log"
PIDFILE="$MODDIR/logs/hp.pid"

# 输出调试信息
echo "[DEBUG] MODDIR: $MODDIR" >> /dev/kmsg
echo "[DEBUG] LOGFILE: $LOGFILE" >> /dev/kmsg
echo "[DEBUG] PIDFILE: $PIDFILE" >> /dev/kmsg

# 创建日志目录
mkdir -p "$MODDIR/logs"
echo "[DEBUG] Created log directory" >> /dev/kmsg

# 检查是否已经在运行
if [ -f "$PIDFILE" ]; then
    OLD_PID=$(cat "$PIDFILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "[DEBUG] Already running, exiting" >> /dev/kmsg
        exit 0
    fi
fi

# 停止旧进程
pkill -9 hyperpredictd 2>/dev/null || true

# 启动守护进程（不使用 shell 重定向，让日志系统自己管理）
echo "[DEBUG] Starting hyperpredictd" >> /dev/kmsg
"$MODDIR/system/bin/hyperpredictd" --mod-dir "$MODDIR" &
PID=$!
echo "$PID" > "$PIDFILE"
echo "[DEBUG] Started with PID: $PID" >> /dev/kmsg

exit 0
