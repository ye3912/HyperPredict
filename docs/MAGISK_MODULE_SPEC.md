# Magisk 模块规范

## 模块结构

```
magisk_module/
├── META-INF/
│   └── com/google/android/
│       ├── update-binary    # 安装脚本（必须）
│       └── updater-script   # 标记文件（必须）
├── system/                  # 系统文件（可选）
│   └── bin/
│       └── hyperpredictd    # 二进制文件
├── service.sh               # 服务启动脚本（可选）
├── uninstall.sh             # 卸载脚本（可选）
├── module.prop              # 模块信息（必须）
└── webroot/                 # WebUI 文件（可选）
```

## 必需文件

### 1. module.prop

模块信息文件，必须包含以下字段：

```
id=hyperpredict
name=HyperPredict
version=v4.2.0
versionCode=420
author=ye3912
description=AI CPU Scheduler with KSU WebUI Support
```

**字段说明:**
- `id`: 模块 ID，只能包含小写字母、数字、下划线
- `name`: 模块名称
- `version`: 版本号，格式建议为 `v{major}.{minor}.{patch}`
- `versionCode`: 版本代码，整数，用于版本比较
- `author`: 作者名称
- `description`: 模块描述

### 2. META-INF/com/google/android/update-binary

安装脚本，必须使用 `#!/sbin/sh`：

```bash
#!/sbin/sh
# Magisk Module Installer Script

OUTFD=$2
ZIPFILE=$3

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
unzip -o "$ZIPFILE" -d "$MODPATH"

# 设置权限
chmod 755 "$MODPATH/system/bin/hyperpredictd"
chmod 755 "$MODPATH/service.sh"
chmod 755 "$MODPATH/uninstall.sh"

echo "HyperPredict 安装完成"
```

**环境变量:**
- `OUTFD`: 输出文件描述符
- `ZIPFILE`: ZIP 文件路径
- `MODPATH`: 模块安装路径
- `KSU`: KernelSU 标志
- `APATCH`: APatch 标志
- `MAGISK_VER`: Magisk 版本

### 3. META-INF/com/google/android/updater-script

标记文件，必须包含：

```
#MAGISK
```

## 可选文件

### 1. service.sh

服务启动脚本，在系统启动时执行：

```bash
#!/system/bin/sh
# HyperPredict 服务启动脚本

# 等待系统启动完成
sleep 15

# 设置环境变量
MODDIR=${0%/*}
LOGFILE="$MODDIR/logs/hp.log"
PIDFILE="$MODDIR/logs/hp.pid"

# 创建日志目录
mkdir -p "$MODDIR/logs"

# 检查是否已经在运行
if [ -f "$PIDFILE" ]; then
    OLD_PID=$(cat "$PIDFILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        exit 0
    fi
fi

# 停止旧进程
pkill -9 hyperpredictd 2>/dev/null || true

# 启动守护进程
nohup "$MODDIR/system/bin/hyperpredictd" > "$LOGFILE" 2>&1 &
PID=$!
echo "$PID" > "$PIDFILE"

exit 0
```

**环境变量:**
- `MODDIR`: 模块目录路径

### 2. uninstall.sh

卸载脚本，在卸载模块时执行：

```bash
#!/system/bin/sh
# HyperPredict 卸载脚本

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
```

**环境变量:**
- `MODDIR`: 模块目录路径

### 3. system/

系统文件目录，文件会被复制到对应的系统目录：

```
system/
├── bin/
│   └── hyperpredictd    # 复制到 /system/bin/
└── etc/
    └── config.conf      # 复制到 /system/etc/
```

## 环境变量

### 安装时（update-binary）

- `MODPATH`: 模块安装路径（例如：`/data/adb/modules/hyperpredict`）
- `ZIPFILE`: ZIP 文件路径
- `OUTFD`: 输出文件描述符
- `KSU`: KernelSU 标志（如果存在）
- `APATCH`: APatch 标志（如果存在）
- `MAGISK_VER`: Magisk 版本（如果存在）

### 运行时（service.sh、uninstall.sh）

- `MODDIR`: 模块目录路径（例如：`/data/adb/modules/hyperpredict`）

## 模块管理器支持

### Magisk

- 完整支持所有功能
- 使用 `MODPATH` 环境变量
- 支持 `service.sh` 和 `uninstall.sh`

### APatch

- 兼容 Magisk 模块规范
- 使用 `MODPATH` 环境变量
- 支持 `service.sh` 和 `uninstall.sh`

### KernelSU

- 兼容 Magisk 模块规范
- 使用 `MODPATH` 环境变量
- 支持 `service.sh` 和 `uninstall.sh`
- 支持 WebUI（通过 `webui=` 字段）

## 注意事项

1. **文件权限**: 所有脚本必须设置执行权限（`chmod 755`）
2. **环境变量**: 使用 `MODDIR` 而不是硬编码路径
3. **错误处理**: 不要在 `service.sh` 中输出太多日志，Magisk 会捕获输出
4. **进程管理**: 使用 PID 文件来管理进程，避免重复启动
5. **清理**: 在 `uninstall.sh` 中清理所有文件和进程

## 示例模块

完整的 HyperPredict 模块示例：

```
magisk_module/
├── META-INF/
│   └── com/google/android/
│       ├── update-binary
│       └── updater-script
├── system/
│   └── bin/
│       └── hyperpredictd
├── logs/
├── webroot/
│   ├── index.html
│   ├── app.js
│   └── styles.css
├── service.sh
├── uninstall.sh
└── module.prop
```

## 参考资料

- [Magisk 模块开发指南](https://topjohnwu.github.io/Magisk/guide.html)
- [KernelSU 模块开发指南](https://kernelsu.org/docs/zh_CN/guide/module-dev.html)
- [APatch 模块开发指南](https://apatch.dev/docs/zh_CN/guide/module-dev.html)
