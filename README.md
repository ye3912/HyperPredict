# HyperPredict

<p align="center">
  <img src="https://img.shields.io/badge/Version-v4.2-blue" alt="Version">
  <img src="https://img.shields.io/badge/License-Apache--2.0-green" alt="License">
  <img src="https://img.shields.io/badge/Platform-Android%20%2B%20Linux-brightgreen" alt="Platform">
  <img src="https://img.shields.io/badge/C%2B%2B-17-orange" alt="C++">
</p>

**HyperPredict** 是一个基于机器学习的 Android/Linux CPU 调度预测守护进程。通过实时采集系统负载特征，结合线性回归和神经网络双模型预测，智能动态调整 CPU 频率与核心绑定策略，优化设备性能与功耗平衡。

## ✨ 特性

### 🤖 双模型预测系统
- **线性回归模型**: 轻量快速，即时响应
- **神经网络模型 (MLP)**: 8→16→8→1 架构，深度学习预测
- **模型热切换**: WebUI 实时切换，一键对比

### ⚡ 高性能调度引擎
- **智能核心绑定**: 异构/全大核架构自适应
- **动态负载均衡**: EMA 平滑算法，减少抖动
- **温控优先级**: 过热自动降级，保护设备

### 📊 WebUI 管理界面
- **120Hz 流畅动画**: Material Design 3 风格
- **实时可视化**: FPS、温度、负载曲线
- **模型参数监控**: 准确率、MAE 实时显示

### 🔌 进程通信
- **WebSocket**: 实时双向推送
- **HTTP REST API**: 状态查询、模型同步
- **自动重连**: 指数退避算法

## 🏗️ 架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         HyperPredict                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐        │
│  │   WebUI     │◄──►│  WebServer  │◄──►│   EventLoop │        │
│  │  (浏览器)    │    │  HTTP/WS    │    │  主事件循环  │        │
│  └─────────────┘    └─────────────┘    └──────┬──────┘        │
│                                               │                 │
│  ┌─────────────┐    ┌─────────────┐    ┌──────▼──────┐        │
│  │  Predictor  │◄──►│   Policy    │◄──►│   System    │        │
│  │  帧率预测   │    │   Engine    │    │  Collector  │        │
│  └─────────────┘    └──────┬──────┘    └─────────────┘        │
│                             │                                    │
│  ┌─────────────┐    ┌──────▼──────┐    ┌─────────────┐        │
│  │ Migration   │◄──►│   Freq      │◄──►│    Core     │        │
│  │ Engine      │    │   Manager   │    │   Binder    │        │
│  │ 核心迁移    │    │  频率管理   │    │  核心绑定   │        │
│  └─────────────┘    └─────────────┘    └─────────────┘        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## 📁 项目结构

```
HyperPredict/
├── include/
│   ├── core/           # 核心组件
│   │   ├── event_loop.h       # 主事件循环
│   │   ├── system_collector.h # 系统数据采集
│   │   ├── lockfree_queue.h  # 无锁队列
│   │   └── logger.h          # 日志系统
│   ├── predict/         # 预测模型
│   │   ├── predictor.h        # 线性回归预测器
│   │   └── feature_extractor.h
│   ├── device/          # 设备驱动
│   │   ├── cpu_freq_manager.h # CPU 频率管理
│   │   ├── migration_engine.h # 核心迁移引擎
│   │   ├── core_binder.h     # 核心绑定
│   │   └── soc_database.h    # SoC 数据库
│   ├── sched/          # 调度策略
│   │   └── policy_engine.h   # 策略引擎
│   └── net/           # 网络通信
│       └── web_server.h     # HTTP/WebSocket 服务器
├── src/
│   ├── core/          # 核心实现
│   ├── predict/       # 预测实现
│   ├── device/       # 设备实现
│   ├── sched/        # 调度实现
│   └── net/          # 网络实现
├── ksuwebui/         # WebUI
│   ├── index.html     # 主页面
│   ├── app.js         # 应用逻辑 + ML 模型
│   ├── styles.css     # M3 样式
│   └── ws-test.html   # WebSocket 测试页
├── scripts/
│   ├── build_taliored.sh
│   ├── device_probe.sh
│   └── run_root.sh
└── CMakeLists.txt
```

## 🔧 构建

### 环境要求

- Android NDK r26b+
- CMake 3.10+
- C++17 编译器

### 本地构建

```bash
# 设置 NDK 路径
export ANDROID_NDK=$HOME/Android/Sdk/ndk/26.1.10909131

# 构建
mkdir build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -GNinja

ninja
```

### GitHub Actions 自动构建

推送代码后自动触发，构建产物在 Actions Artifacts 中。

## 📦 安装

### 支持的平台

- ✅ **Magisk** - 完整支持
- ✅ **APatch** - 完整支持
- ✅ **KernelSU** - 完整支持（含 WebUI）

### 自动安装（推荐）

1. 从 [GitHub Releases](https://github.com/ye3912/HyperPredict/releases) 下载最新版本
2. 在对应的模块管理器中刷入 ZIP 文件
3. 重启设备

### 手动安装

```bash
# 推送二进制
adb push build/hyperpredictd /data/local/tmp/
adb shell chmod +x /data/local/tmp/hyperpredictd

# 启动守护进程
adb shell /data/local/tmp/hyperpredictd

# 查看日志
adb shell logcat -s HyperPredict
```

### 使用脚本安装

```bash
# 使用通用安装脚本
adb push scripts/install_module.sh /data/local/tmp/
adb shell su -c "sh /data/local/tmp/install_module.sh"

# 卸载
adb push scripts/uninstall_module.sh /data/local/tmp/
adb shell su -c "sh /data/local/tmp/uninstall_module.sh"
```

### 模块结构

```
/data/adb/modules/hyperpredict/
├── system/bin/hyperpredictd    # 主程序
├── logs/hp.log                 # 日志文件
├── webroot/                    # WebUI 文件
├── service.sh                  # 服务启动脚本
└── uninstall.sh                # 卸载脚本
```

## 🌐 WebUI 使用

### 访问

```
http://localhost:8081/
```

### 功能面板

| 面板 | 功能 |
|------|------|
| 📊 监控 | FPS、温度、电池、CPU 集群状态 |
| 🤖 预测 | 神经网络/线性回归模型切换、参数可视化 |
| ⚡ 调度 | 均衡/游戏/性能模式、uclamp 配置 |
| 🌡️ 温控 | 激进/均衡/静音预设 |

### API 接口

| 端点 | 方法 | 描述 |
|------|------|------|
| `/api/status` | GET | 获取系统状态 |
| `/api/model` | GET | 获取模型权重 |
| `/api/model` | POST | 设置模型权重 |
| `/api/command` | POST | 发送命令 |
| `/ws` | WebSocket | 实时推送 |

### WebSocket 命令

```javascript
// 连接
const ws = new WebSocket('ws://localhost:8081/ws');

// 发送命令
ws.send(JSON.stringify({ cmd: 'set_mode', params: { mode: 'game' } }));

// 接收实时数据
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log(data);
};
```

## 🤖 预测模型

### 线性回归

```cpp
pred = w_util * util + w_rq * run_queue + bias
pred += trend * 0.5
```

### 神经网络 (MLP)

```
输入层 (8) → 隐藏层1 (16, LeakyReLU) → 隐藏层2 (8, LeakyReLU) → 输出层 (1)
```

**输入特征:**
- `cpu_util`: CPU 利用率 (0-1024)
- `run_queue`: 运行队列长度
- `wakeups`: 唤醒次数/100ms
- `frame_interval`: 帧间隔 (微秒)
- `touch_rate`: 触摸采样率
- `thermal_margin`: 温控余量
- `battery`: 电池电量
- `is_gaming`: 游戏模式标志

## 📈 支持的 SoC

| 厂商 | 芯片 |
|------|------|
| Snapdragon | 8 Elite Gen 5, 8 Gen 3/2/1, 888, 865, 855... |
| MediaTek | Dimensity 9300/9400, 9200, 8200... |
| Huawei | Kirin 9000/9010, 8300... |
| Samsung | Exynos 2400/2200/2100 |
| Google | Tensor G1/G2/G3/G4 |

## 📝 配置

### 热路径

| 路径 | 用途 |
|------|------|
| `/proc/stat` | CPU 使用率 |
| `/proc/loadavg` | 运行队列 |
| `/sys/devices/system/cpu/cpu*/cpufreq/policy*/scaling_*` | 频率控制 |
| `/sys/class/thermal/thermal_zone*/temp` | 温度监控 |

## 📜 许可证

Apache License 2.0

## 🔗 相关链接

- [GitHub Repository](https://github.com/ye3912/HyperPredict)
- [Actions Build](https://github.com/ye3912/HyperPredict/actions)
- [Releases](https://github.com/ye3912/HyperPredict/releases)
- [脚本使用指南](scripts/README.md)

## 📝 开发指南

### 发布新版本

```bash
# 使用发布脚本
chmod +x scripts/release.sh
./scripts/release.sh

# 脚本会自动：
# 1. 更新版本号
# 2. 提交更改
# 3. 推送到远程
# 4. 创建 Git 标签
# 5. 触发 GitHub Actions 构建
```

### 本地开发工作流

```bash
# 1. 修改代码
vim src/...

# 2. 本地构建
./scripts/build.sh

# 3. 测试
adb push build/hyperpredictd /data/local/tmp/
adb shell su -c "cp /data/local/tmp/hyperpredictd /data/adb/modules/hyperpredict/system/bin/"

# 4. 查看日志
adb shell tail -f /data/adb/modules/hyperpredict/logs/hp.log
```

### 脚本说明

| 脚本 | 功能 |
|------|------|
| `scripts/build.sh` | 本地构建 |
| `scripts/package_module.sh` | 打包模块 |
| `scripts/update_version.sh` | 更新版本号 |
| `scripts/release.sh` | 快速发布 |
| `scripts/install_module.sh` | 通用安装脚本 |
| `scripts/uninstall_module.sh` | 通用卸载脚本 |

详细说明请参考 [脚本使用指南](scripts/README.md)。
