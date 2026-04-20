# HyperPredict

<p align="center">
  <img src="https://img.shields.io/badge/Version-v4.3-blue" alt="Version">
  <img src="https://img.shields.io/badge/License-Apache--2.0-green" alt="License">
  <img src="https://img.shields.io/badge/Platform-Android%20%2B%20Linux-brightgreen" alt="Platform">
  <img src="https://img.shields.io/badge/C%2B%2B-17-orange" alt="C++">
</p>

**HyperPredict** 是一个基于机器学习的 Android/Linux CPU 调度预测守护进程。通过实时采集系统负载特征，结合线性回归和神经网络双模型预测，智能动态调整 CPU 频率与核心绑定策略，优化设备性能与功耗平衡。

## ✨ 特性

### 🤖 多模型预测系统
- **线性模型 (LINEAR)**: 轻量快速，即时响应
- **神经网络模型 (NEURAL)**: 8→16→8→1 MLP 架构，深度学习预测
- **混合模型 (HYBRID)**: 线性 + 神经网络协同决策
- **FTRL 在线学习**: 每 10 次预测自动更新权重，适配设备特性
- **模型热切换**: WebUI 实时切换，一键对比

### ⚡ 高性能调度引擎
- **智能核心绑定**: 异构/全大核架构自适应 (E-Mapper 任务分类)
- **动态负载均衡**: EMA 平滑算法，减少抖动
- **温控优先级**: 过热自动降级，保护设备
- **冷却期机制**: 防止频繁迁移造成的性能抖动
- **FreqMapTable O(1)**: 预计算频率查询表，极速响应

### 📊 WebUI 管理界面
- **120Hz 流畅动画**: Material Design 3 风格
- **实时可视化**: FPS、温度、负载曲线、核心状态
- **模型参数监控**: 准确率、MAE 实时显示
- **任务分类监控**: COMPUTE/MEMORY/IO 实时显示

### 🔌 进程通信
- **WebSocket**: 实时双向推送
- **HTTP REST API**: 状态查询、模型同步
- **自动重连**: 指数退避算法

## 🏗️ 架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         HyperPredict v4.3                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐        │
│  │   WebUI     │◄──►│  WebServer  │◄──►│   EventLoop │        │
│  │  (浏览器)    │    │  HTTP/WS    │    │  主事件循环  │        │
│  └─────────────┘    └──────┬──────┘    └──────┬──────┘        │
│                            │                  │                 │
│  ┌─────────────┐    ┌──────���──────┐    ┌──────▼──────┐        │
│  │  Predictor  │◄──►│   Policy    │◄──►│   System    │        │
│  │ 帧率预测     │    │   Engine    │    │  Collector  │        │
│  │ (FTRL)      │    │ (FreqMap)   │    │  (TTL)      │        │
│  └──────┬──────┘    └──────┬──────┘    └─────────────┘        │
│         │                  │                                   │
│  ┌──────▼──────┐    ┌──────▼──────┐    ┌─────────────┐        │
│  │Migration   │◄──►│   Freq      │◄──►│   Core     │        │
│  │Engine      │    │   Manager   │    │   Binder   │        │
│  │(E-Mapper)  │    │   (LUT)     │    │(Cooperative)│        │
│  └─────────────┘    └─────────────┘    └─────────────┘        │
│                                                                  │
│  ┌─────────────┐    ┌─────────────┐                           │
│  │  Hardware   │◄──►│   SoC       │                           │
│  │  Analyzer   │    │  Database   │                           │
│  └─────────────┘    └─────────────┘                           │
└─────────────────────────────────────────────────────────────────┘
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

### 线性模型 (LINEAR)

```cpp
pred = w_util * util + w_rq * run_queue + w_wakeups * wakeups + bias
pred += trend * acceleration_factor
```

### 神经网络 (NEURAL)

```
输入层 (8) → 隐藏层1 (16, ReLU) → 隐藏层2 (8, ReLU) → 输出层 (1)
         264 权重参数
```

**输入特征:**
- `cpu_util / 1024`: CPU 利用率归一化
- `run_queue / 32`: 运行队列长度归一化
- `wakeups / 100`: 唤醒次数归一化
- `frame_interval / 20000`: 帧间隔归一化
- `touch_rate / 20`: 触摸采样率归一化
- `(thermal_margin + 30) / 60`: 温控余量归一化
- `battery / 100`: 电池电量归一化
- `is_gaming`: 游戏模式标志

### FTRL 在线学习

HyperPredict v4.3 引入了 FTRL (Follow The Regularized Leader) 在线学习器：

```cpp
// FTRL 特点:
// 1. 每 10 次预测才更新一次，开销极小
// 2. L2 正则化防止过拟合
// 3. 自适应学习率，无需手动调参
// 4. 内存占用仅 ~2KB
```

**预期收益**: +15% 预测准确率

### 多时间尺度 EMA

```cpp
// 不同窗口的响应速度
alpha_10ms = 0.7f   // 快速响应
alpha_50ms = 0.3f   // 中等响应
alpha_200ms = 0.1f // 平滑
alpha_500ms = 0.05f // 长趋势
```

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
