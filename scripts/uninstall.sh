#!/system/bin/sh
# HyperPredict 卸载脚本
# 符合 Magisk 模块规范

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
