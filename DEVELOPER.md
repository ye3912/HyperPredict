# HyperPredict 开发者文档

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v4.2.0 | 2026-04 | 重大更新: Bug修复+性能优化 |
| v4.1.0 | 2026-03 | CooperativeScheduler (2026设计) |
| v4.0.0 | 2026-02 | 双模型预测系统 |

---

## 目录

1. [架构概述](#1-架构概述)
2. [核心模块设计](#2-核心模块设计)
3. [API 参考](#3-api-参考)
4. [构建指南](#4-构建指南)
5. [测试指南](#5-测试指南)
6. [贡献指南](#6-贡献指南)

---

## 1. 架构概述

### 1.1 系统架构

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                     HyperPredict v4.2                        │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌────────────┐    ┌────────────┐    ┌────────────┐        │
│  │   WebUI    │◄──►│ WebServer  │◄──►│EventLoop  │        │
│  │ (浏览器)   │    │ HTTP/WS   │    │主事件循环 │        │
│  └────────────┘    └────────────┘    └──────┬────┘        │
│                                            │                │
│  ┌────────────┐    ┌────────────┐    ┌──────▼────┐        │
│  │  Predictor │◄──►│  Policy   │◄──►│ System    │        │
│  │ 帧率预测   │    │  Engine   │    │ Collector│        │
│  └─────┬──────┘    └──────┬────┘    └────────────┘        │
│        │                  │                             │
│  ┌────▼─────┐    ┌──────▼─────┐    ┌─────────────┐        │
│  │ Scene    │    │   Freq     │    │   Core     │        │
│  │Classifier│    │  Manager   │    │  Binder   │        │
│  └─────────┘    └────────────┘    └─────────────┘        │
│                                              │
│  ┌────────────┐    ┌────────────┐    ┌─────────────┐        │
│  │Migration  │◄──►│  SoC      │    │  Hardware  │        │
│  │ Engine    │    │ Database  │    │  Analyzer │        │
│  └───────────     └───────────     └────────────┘        │
└──────────────────────────────────────────────────────────────┘
```

### 1.2 数据流

```
采集周期 (100ms)
    │
    ▼
┌──────────┐     ┌──────────┐     ┌──────────┐
│ System   │────►│ Predictor│────►│  Policy  │
│ Collector│     │          │     │  Engine  │
└──────────┘     ���──────────┘     └────┬────┘
                                     │
                                     ▼
                              ┌────────────┐
                              │   Freq     │
                              │  Manager   │
                              └────┬─────┘
                                   │
           ┌────────────────────────┼────────────────────────┐
           ▼                        ▼                        ▼
    ┌────────────┐         ┌────────────┐         ┌────────────┐
    │  Sysfs     │         │Migration  │         │   Core    │
    │  Writer   │         │  Engine   │         │  Binder   │
    └────────────┘         └────────────┘         └────────────┘
```

### 1.3 核心特性

| 特性 | 说明 |
|------|------|
| 双模型预测 | 线性回归 + 神经网络 MLP (8→16→8→1) |
| 协作式调度 | CooperativeScheduler (EAS + Game Driver + 预测式) |
| 多时间尺度 | 10ms/50ms/200ms/500ms EMA |
| 场景识别 | IDLE/LIGHT/MEDIUM/HEAVY/BOOST/IO_WAIT |

---

## 2. 核心模块设计

### 2.1 EventLoop (事件循环)

**职责**: 主控制循环，协调各模块

**位置**: `src/core/event_loop.cpp`, `include/core/event_loop.h`

```cpp
class EventLoop {
public:
    bool init() noexcept;
    void start() noexcept;
    void stop() noexcept;

private:
    void collect() noexcept;      // 采集系统特征
    void process() noexcept;     // 处理预测和调频
    void apply_freq_config() noexcept; // 应用频率配置
};
```

**关键逻辑**:
1. 使用 epoll + timerfd 实现事件驱动
2. Rate limiting: 游戏 500us / 日常 2000us
3. 每 5 个周期执行一次迁移决策

### 2.2 Predictor (预测器)

**职责**: 帧率预测，支持多模型

**位置**: `src/predict/predictor.cpp`, `include/predict/predictor.h`

```cpp
class Predictor {
public:
    enum class Model { LINEAR, NEURAL, HYBRID };
    
    float predict(const LoadFeature& features) noexcept;
    float predict_linear(const LoadFeature&) noexcept;
    float predict_neural(const LoadFeature&) noexcept;
    float predict_scene_aware(const LoadFeature&) noexcept;
    
    void update_multiscale_features(const LoadFeature&, uint64_t now_ns) noexcept;
    void train(const LoadFeature&, float actual_fps) noexcept;
    std::future<void> train_async(const LoadFeature&, float actual_fps) noexcept;
};
```

**预测模型**:

```
输入特征 (8维):
  - cpu_util / 1024.0f          [0-1]
  - run_queue_len / 32.0f           [0-1]
  - wakeups_100ms / 100.0f        [0-1]
  - frame_interval_us / 20000.0f    [0-1]
  - touch_rate_100ms / 20.0f        [0-1]
  - (thermal_margin + 30) / 60.0f   [0-1]
  - battery_level / 100.0f          [0-1]
  - is_gaming ? 1.0f : 0.0f     [0-1]
                   │
                   ▼
           神经网络 (8→16→8→1)
           LeakyReLU 激活
                   │
                   ▼
              输出: 预测 FPS [0-144]
```

**多时间尺度 EMA**:

```cpp
// 不同窗口的响应速度
constexpr float alpha_10ms = 0.7f;   // 快速响应
constexpr float alpha_50ms = 0.3f;   // 中等响应
constexpr float alpha_200ms = 0.1f;  // 平滑
constexpr float alpha_500ms = 0.05f;  // 长趋势
```

### 2.3 PolicyEngine (策略引擎)

**职责**: CPU 频率决策

**位置**: `src/sched/policy_engine.cpp`, `include/sched/policy_engine.h`

```cpp
class PolicyEngine {
public:
    void init(const BaselinePolicy&) noexcept;
    FreqConfig decide(const LoadFeature& f, float target_fps, const char* scene) noexcept;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

**频率决策流程**:

```
1. 多时间尺度 EMA (10ms/50ms/200ms)
2. 趋势计算 (加速度)
3. schedutil 公式: freq = 1.25 * max_freq * util
4. FPS 误差修正
5. IO-Wait Boost
6. 触摸加速
7. 趋势修正
8. 温控缩放
9. 边界约束
10. Rate Limiting
```

**FreqMapTable (频率映射表)**:

```cpp
// 预计算 1024 个条目的映射 (O(1) 查找)
void init(uint32_t max_freq, const std::vector<uint32_t>& steps) noexcept {
    table_.resize(1024);
    for (int util = 0; util < 1024; util++) {
        // 二分查找最近的频点
        table_[util] = binary_search(target_freq);
    }
}
```

### 2.4 MigrationEngine (迁移引擎)

**职责**: CPU 核心迁移决策

**位置**: `src/device/migration_engine.cpp`, `include/device/migration_engine.h`

```cpp
class MigrationEngine {
public:
    void init(const HardwareProfile&) noexcept;
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    MigResult decide(int cur, uint32_t therm, bool game) noexcept;
    
private:
    MigPolicy policy_{MigPolicy::Balanced};
    uint8_t cool_{0};  // 冷却期
};
```

**迁移决策**:

```
优先级:
1. 温控紧急降级 (therm < 5)
2. 冷却期检查
3. 游戏模式: 强制大核
4. 轻负载保护 (util < 128 && rq < 2)
5. 中等负载调整 (util ∈ [128, 512))
6. 高负载均衡 (rq > 3)
```

**轻负载保护**:

```cpp
// 避免小核开销: 迁移成本 ≈ 0.5ms
// 如果预估节省 < 迁移成本，则不迁移
if (estimated_save > MIGRATION_COST_US) {
    migrate();
}
```

### 2.5 CooperativeScheduler (协作式调度器)

**职责**: 智能核心绑定 (2026 设计)

**位置**: `include/device/core_binder.h`

```cpp
class CooperativeScheduler {
public:
    static constexpr int MAX_CPUS = 8;
    static constexpr int HISTORY_SIZE = 8;
    
    SchedDecision decide(
        int current_cpu,
        uint32_t util,
        uint32_t rq,
        bool is_game,
        bool is_foreground,
        uint32_t target_fps
    ) noexcept;
    
private:
    void calculate_trend(int cpu);
    float predict_load(int cpu);
    float estimate_power_save(int from, int to);
};
```

**预测式调度**:

```cpp
// 1. 负载趋势预测 (线性回归)
float calculate_trend(int cpu) {
    // y = mx + b
    float slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    return slope;  // 变化速度
}

// 2. 负载预测
float predict_load(int cpu) {
    float base = current + velocity * 2.0f;
    float weighted_avg = 历史加权平均;
    return base * 0.7f + weighted_avg * 0.3f;
}
```

### 2.6 SystemCollector (系统采集)

**职责**: 系统特征采集

**位置**: `src/core/system_collector.cpp`, `include/core/system_collector.h`

```cpp
struct LoadFeature {
    uint32_t cpu_util;        // 0-1024
    uint32_t run_queue_len;   // 0-255
    uint32_t wakeups_100ms;   // 0-1000
    uint32_t frame_interval_us; // 微秒
    uint8_t touch_rate_100ms;
    int8_t thermal_margin; // 0-60
    uint8_t battery_level;   // 0-100
    bool is_gaming;
};

class SystemCollector {
public:
    LoadFeature collect() noexcept;
    bool is_gaming_scene() noexcept;
};
```

**采集优化**:

```cpp
// 预打开文件描述符
SystemCollector::SystemCollector() {
    thermal_fds_[0] = ::open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY | O_CLOEXEC);
    // ...
}

// 使用 pread 读取
ssize_t n = pread(thermal_fds_[i], buf, sizeof(buf) - 1, 0);
```

---

## 3. API 参考

### 3.1 核心类 API

#### EventLoop

| 方法 | 说明 | 返回 |
|------|------|------|
| `init()` | 初始化 | `bool` |
| `start()` | 启动主��环 | `void` |
| `stop()` | 停止 | `void` |

#### Predictor

| 方法 | 说明 | 返回 |
|------|------|------|
| `predict(features)` | 预测 FPS | `float` |
| `set_model(Model)` | 设置模型 | `void` |
| `get_model()` | 获取模型 | `Model` |
| `train(features, actual)` | 训练 | `void` |

#### PolicyEngine

| 方法 | 说明 | 返回 |
|------|------|------|
| `decide(f, fps, scene)` | 决策频率 | `FreqConfig` |
| `set_io_wait_boost(bool)` | 设置 IO boost | `void` |

#### MigrationEngine

| 方法 | 说明 | 返回 |
|------|------|------|
| `update(cpu, util, rq)` | 更新负载 | `void` |
| `decide(cpu, therm, game)` | 决策迁移 | `MigResult` |
| `set_policy(Policy)` | 设置策略 | `void` |

### 3.2 数据结构

#### LoadFeature

```cpp
struct LoadFeature {
    uint32_t cpu_util;        // CPU 利用率 (0-1024)
    uint32_t run_queue_len;   // 运行队列长度
    uint32_t wakeups_100ms; // 唤醒次数/100ms
    uint32_t frame_interval_us; // 帧间隔 (微秒)
    uint8_t touch_rate_100ms; // 触摸采样率
    int8_t thermal_margin; // 温控余量 (0-60)
    uint8_t battery_level; // 电池电量 (0-100)
    bool is_gaming;       // 游戏模式
};
```

#### FreqConfig

```cpp
struct FreqConfig {
    uint32_t target_freq; // 目标频率 (kHz)
    uint32_t min_freq;   // 最小频率
    uint8_t uclamp_min; // uclamp.min (0-100)
    uint8_t uclamp_max; // uclamp.max (0-100)
};
```

#### MigResult

```cpp
struct MigResult {
    int target;      // 目标 CPU
    bool go;         // 是否迁移
    bool thermal;   // 温控紧急
};
```

### 3.3 枚举类型

#### MigPolicy

```cpp
enum class MigPolicy : uint8_t {
    Conservative,  // 保守模式 - 省电
    Balanced,      // 平衡模式 - 默认
    Aggressive     // 激进模式 - 性能
};
```

#### SchedScene

```cpp
enum class SchedScene : uint8_t {
    IDLE,      // 待机
    LIGHT,     // 轻负载
    MEDIUM,    // 中负载
    HEAVY,     // 重负载
    BOOST,     // 加速
    IO_WAIT,   // IO 等待
    SCENE_COUNT
};
```

---

## 4. 构建指南

### 4.1 环境要求

| 组件 | 版本 |
|------|------|
| Android NDK | r26b+ |
| CMake | 3.10+ |
| C++ | 20 (NDK r26b), 17 (older) |
| Ninja | 推荐 |

### 4.2 本地构建

```bash
# 1. 克隆仓库
git clone https://github.com/ye3912/HyperPredict.git
cd HyperPredict

# 2. 设置 NDK
export ANDROID_NDK=$HOME/Android/Sdk/ndk/26.1.10909131

# 3. 创建构建目录
mkdir build && cd build

# 4. 配置
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_BUILD_TYPE=Release \
    -GNinja

# 5. 构建
ninja
```

### 4.3 Linux 构建 (测试用)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 4.4 GitHub Actions 构建

推送代码后自动触发:

```yaml
# .github/workflows/magisk.yml
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -GNinja
          ninja
```

---

## 5. 测试指南

### 5.1 单元测试

位置: `tests/`

```bash
# 构建测试
cmake .. -DBUILD_TESTING=ON
make test
```

### 5.2 手动测试

```bash
# 启动守护进程
./build/hyperpredictd

# 查看日志
adb logcat -s HyperPredict

# WebUI 访问
http://localhost:8081/
```

### 5.3 API 测试

```bash
# 获取状态
curl http://localhost:8081/api/status

# 设置模式
curl -X POST http://localhost:8081/api/command \
  -H "Content-Type: application/json" \
  -d '{"cmd":"set_mode","params":{"mode":"game"}}'
```

---

## 6. 贡献指南

### 6.1 开发流程

```
1. Fork 仓库
2. 创建分支: git checkout -b feature/xxx
3. 开发并测试
4. 提交: git commit -m "描述"
5. 推送: git push origin feature/xxx
6. 创建 Pull Request
```

### 6.2 代码规范

| 规范 | 要求 |
|------|------|
| C++ 标准 | C++17 / C++20 |
| 缩进 | 4 空格 |
| 命名 | snake_case |
| 注释 | 英文 |

### 6.3 提交规范

```
<类型>: <描述>

类型:
  - feat: 新功能
  - fix: Bug 修复
  - perf: 性能优化
  - docs: 文档
  - refactor: 重构
```

### 6.4 代码审查检查项

- [ ] 编译通过 (NDK + Linux)
- [ ] 无新增警告
- [ ] 无内存泄漏
- [ ] 单元测试通过

---

## 附录

### A. 支持的 SoC

| 厂商 | 芯片 |
|------|------|
| Snapdragon | 8 Elite Gen 5, 8 Gen 3/2/1, 888, 865, 855 |
| MediaTek | Dimensity 9300/9400, 9200, 8200 |
| Huawei | Kirin 9000/9010, 8300 |
| Samsung | Exynos 2400/2200/2100 |
| Google | Tensor G1/G2/G3/G4 |

### B. 热路径

| 路径 | 用途 |
|------|------|
| `/proc/stat` | CPU 使用率 |
| `/proc/loadavg` | 运行队列 |
| `/sys/devices/system/cpu/cpu*/cpufreq/*/scaling_*` | 频率控制 |
| `/sys/class/thermal/thermal_zone*/temp` | 温度 |
| `/dev/cpuctl/cpu*/uclamp.*` | uclamp 控制 |

### C. 性能指标

| 指标 | 目标 |
|------|------|
| CPU 占用 | < 1% |
| 内存占用 | < 5MB |
| 调度延迟 | < 10ms |
| 预测误差 | MAE < 5 FPS |

### D. 版本历史

详见 CHANGELOG.md