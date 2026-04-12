#!/system/bin/sh
HP="/data/adb/hyperpredict"
mkdir -p $HP/bin
cp build/hyperpredictd $HP/bin/ 2>/dev/null || cp $1 $HP/bin/
chmod 755 $HP/bin/hyperpredictd
setenforce 0 2>/dev/null || true
nohup $HP/bin/hyperpredictd > /dev/null 2>&1 &
echo "[✓] HyperPredict running (PID: $(pidof hyperpredictd))"