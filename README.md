# HyperPredict

**HyperPredict** 是一个基于 FTRL 机器学习算法的 Android CPU 调度预测守护进程，通过实时采集系统负载特征，智能预测并动态调整 CPU 频率策略，优化设备性能与功耗平衡。

## 特性

- **FTRL 在线学习**: 实时更新预测模型，自适应不同应用场景
- **无锁队列**: 高效的多线程数据传递，降低延迟
- **分段 LRU 缓存**: 减少锁竞争，提升缓存吞吐
- **Fallback 机制**: 预测异常时自动切换安全策略
- **设备自适应**: 自动探测 CPU 拓扑，生成设备专属配置

## 构建

### 前置要求

- Android NDK r26b 或更高版本
- CMake 3.20+
- C++20 编译器

### 本地构建

```bash
# 设置 NDK 路径
export ANDROID_NDK=$HOME/Android/Sdk/ndk/26.1.10909131

# 设备探测（可选，生成 device/hardware.h）
./scripts/device_probe.sh

# 构建
mkdir build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 使用脚本构建

```bash
./scripts/build_taliored.sh
```

## 安装与运行

### Magisk 模块方式

将编译产物放入 Magisk 模块目录：

```
/data/adb/modules/hyperpredict/
├── bin/hyperpredictd
├── service.sh
└── module.prop
```

### 手动运行

```bash
adb push build/hyperpredictd /data/local/tmp/
adb shell chmod +x /data/local/tmp/hyperpredictd
adb shell /data/local/tmp/hyperpredictd
```

### Root 设备部署

```bash
./scripts/run_root.sh
```

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                      EventLoop                               │
│  ┌──────────────┐    ┌──────────────┐                       │
│  │   collect    │───▶│  FeatureQueue│───▶  dispatch         │
│  │  (采集线程)   │    │   (无锁队列)  │     (决策线程)        │
│  └──────────────┘    └──────────────┘                       │
│         │                   │                               │
│         ▼                   ▼                               │
│  ┌──────────────┐    ┌──────────────┐                       │
│  │SystemCollector│   │ PolicyEngine │                       │
│  │ (系统数据采集) │   │  (策略决策)   │                       │
│  └──────────────┘    └──────────────┘                       │
│                            │                                │
│                            ▼                                │
│                     ┌──────────────┐                        │
│                     │ SysfsWriter  │                        │
│                     │ (频率写入)    │                        │
│                     └──────────────┘                        │
└─────────────────────────────────────────────────────────────┘
```

## 核心组件

| 组件 | 文件 | 功能 |
|------|------|------|
| FTRL Predictor | `predict/predictor.cpp` | 在线学习预测模型 |
| Feature Extractor | `predict/feature_extractor.cpp` | 负载特征提取与 EWMA 计算 |
| Fallback Manager | `predict/fallback_manager.cpp` | 异常检测与安全降级 |
| Policy Engine | `sched/policy_engine.cpp` | 频率策略决策 |
| LRU Cache | `cache/lru_cache.cpp` | 应用场景配置缓存 |
| Sysfs Writer | `kernel/sysfs_writer.cpp` | CPU 频率 sysfs 写入 |
| System Collector | `core/system_collector.cpp` | 系统负载实时采集 |

## 配置

### 热点路径

| 路径 | 用途 |
|------|------|
| `/proc/stat` | CPU 使用率 |
| `/proc/loadavg` | 运行队列长度 |
| `/sys/devices/system/cpu/cpufreq/policy*/scaling_*` | 频率控制 |
| `/sys/class/thermal/thermal_zone*/temp` | 温度监控 |

## 许可证

Apache License 2.0

## 版本历史

- **v2.0.0**: 重构架构，添加真实数据采集、Fallback 机制
- **v1.x**: 基础预测框架