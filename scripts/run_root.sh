#!/system/bin/sh
HP="/data/adb/hyperpredict"
mkdir -p $HP/bin
cp build/hyperpredictd $HP/bin/ 2>/dev/null || cp $1 $HP/bin/
chmod 755 $HP/bin/hyperpredictd

# SELinux: Try permissive domain first, fallback to setenforce
if command -v chcon >/dev/null 2>&1; then
    chcon u:r:init:s0 $HP/bin/hyperpredictd 2>/dev/null || true
fi

# Start daemon
nohup $HP/bin/hyperpredictd > /dev/null 2>&1 &
echo "[✓] HyperPredict running (PID: $(pidof hyperpredictd))"

# Note: For proper SELinux support, create a custom policy:
#   hyperpredict.te: type hyperpredict, domain;
#   See: https://github.com/topjohnwu/Magisk/blob/master/docs/tools.md