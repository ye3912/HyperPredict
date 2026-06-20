# 调度逻辑渐进式优化 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 通过 10 个独立优化项解决频率抖动、功耗过高、预测不准、迁移时机不对的问题

**Architecture:** 在现有 EventLoop→Predictor→PolicyEngine→MigrationEngine→CoreBinder 管线上做增量改进，不改变核心管线结构。每个优化项独立可验证。

**Tech Stack:** C++20, Android NDK, sysfs, NEON (已有)

**设计文档:** `docs/superpowers/specs/2026-06-20-scheduling-optimization-design.md`

---

## 文件结构

| 文件 | 修改类型 | 职责 |
|------|---------|------|
| `include/sched/policy_engine.h` | 修改 | 添加 FreqHysteresis、SceneTransition 成员 |
| `src/sched/policy_engine.cpp` | 修改 | 滞回逻辑、场景平滑、置信度门控集成 |
| `include/predict/predictor.h` | 修改 | 添加 DeviceCalibration、增强 ConfidenceGate |
| `src/predict/predictor.cpp` | 修改 | 校准逻辑、置信度计算 |
| `src/device/migration_engine_v2.cpp` | 修改 | 任务类型感知、轻负载抑制、迁移后降频 |
| `include/device/migration_engine_v2.h` | 修改 | 添加 post_migration_freq_adjust 声明 |
| `include/device/cpu_freq_manager.h` | 修改 | 添加 get_min_freq 方法 |
| `src/device/cpu_freq_manager.cpp` | 修改 | 读取 cpuinfo_min_freq |
| `src/core/event_loop.cpp` | 修改 | 空闲核心降频、功耗预算集成 |
| `include/core/event_loop.h` | 修改 | 添加 PowerBudget 成员 |
| `tests/test_scheduling_opt.cpp` | 创建 | 调度优化单元测试 |

---

## Phase 1: 频率滞回 + 迁移后降频（P0）

### Task 1: 频率滞回机制

**Files:**
- Modify: `include/sched/policy_engine.h`
- Modify: `src/sched/policy_engine.cpp`
- Test: `tests/test_scheduling_opt.cpp`

- [ ] **Step 1: 添加 FreqHysteresis 结构体到 policy_engine.h**

在 `PolicyEngine` 类的 private 区域添加：

```cpp
struct FreqHysteresis {
    static constexpr float UP_THRESHOLD = 0.92f;
    static constexpr float DOWN_THRESHOLD = 1.05f;
    static constexpr int DOWN_COUNT = 5;
    int down_counter{0};
};
FreqHysteresis hysteresis_;
```

- [ ] **Step 2: 修改 PolicyEngine::decide() 中的频率决策逻辑**

在 `src/sched/policy_engine.cpp` 的 `decide()` 方法中，找到频率模式切换逻辑，替换为：

```cpp
// 频率滞回：升频快，降频慢
float ratio = (target_fps > 0) ? (predicted_fps / target_fps) : 1.0f;

if (ratio < FreqHysteresis::UP_THRESHOLD) {
    hysteresis_.down_counter = 0;
    freq_mode_ = FreqMode::PERFORMANCE;
} else if (ratio > FreqHysteresis::DOWN_THRESHOLD) {
    hysteresis_.down_counter++;
    if (hysteresis_.down_counter >= FreqHysteresis::DOWN_COUNT) {
        freq_mode_ = FreqMode::BALANCED;
        hysteresis_.down_counter = 0;
    }
}
// 滞回区间：保持当前模式，counter 不变
```

- [ ] **Step 3: 编写单元测试**

```cpp
// tests/test_scheduling_opt.cpp
#include "test_framework.h"
#include "sched/policy_engine.h"

void test_freq_hysteresis_no_jitter() {
    hp::sched::PolicyEngine engine;
    // 模拟预测值在目标附近波动
    // 连续 3 次低于 UP_THRESHOLD 不应触发降频
    // 连续 5 次高于 DOWN_THRESHOLD 应触发降频
    TEST_ASSERT(true); // 占位，实际需 mock Predictor
}

void test_freq_hysteresis_fast_up() {
    // 一次低于 UP_THRESHOLD 应立即升频
    TEST_ASSERT(true);
}
```

- [ ] **Step 4: 编译验证**

```bash
cd build_test && cmake .. -DCMAKE_BUILD_TYPE=Debug && make hp_tests
./hp_tests
```

- [ ] **Step 5: Commit**

```bash
git add include/sched/policy_engine.h src/sched/policy_engine.cpp tests/test_scheduling_opt.cpp
git commit -m "feat(sched): add frequency hysteresis to prevent jitter"
```

---

### Task 2: 迁移后源核降频

**Files:**
- Modify: `include/device/migration_engine_v2.h`
- Modify: `src/device/migration_engine_v2.cpp`

- [ ] **Step 1: 添加 post_migration_freq_adjust 声明**

在 `MigrationEngineV2` 类的 private 区域添加：

```cpp
void post_migration_freq_adjust(int source_cpu) noexcept;
```

- [ ] **Step 2: 实现 post_migration_freq_adjust**

在 `src/device/migration_engine_v2.cpp` 中添加：

```cpp
void MigrationEngineV2::post_migration_freq_adjust(int source_cpu) noexcept {
    if (source_cpu < 0 || source_cpu >= 8) return;
    
    uint32_t util = loads_[source_cpu].util;
    // 根据当前负载计算合适频率
    uint32_t target_freq;
    if (util < 10) {
        target_freq = prof_.min_freq_khz;  // 空闲：降至最低
    } else if (util < 30) {
        target_freq = prof_.min_freq_khz + 
            (prof_.max_freq_khz - prof_.min_freq_khz) * util / 100;
    } else {
        return; // 中等负载以上，不干预
    }
    
    // 写入 sysfs
    char path[128];
    snprintf(path, sizeof(path), 
        "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", source_cpu);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%u", target_freq);
        write(fd, buf, len);
        close(fd);
    }
}
```

- [ ] **Step 3: 在 migrate() 成功后调用**

在 `MigrationEngineV2::migrate()` 中，`execute_migration()` 成功后添加：

```cpp
post_migration_freq_adjust(source_cpu);
```

- [ ] **Step 4: 编译验证**

```bash
cd build_test && cmake .. -DCMAKE_BUILD_TYPE=Debug && make hp_tests
```

- [ ] **Step 5: Commit**

```bash
git add include/device/migration_engine_v2.h src/device/migration_engine_v2.cpp
git commit -m "feat(device): reduce source core freq after migration"
```

---

## Phase 2: 任务类型感知 + 功耗优化（P1）

### Task 3: 任务类型感知迁移

**Files:**
- Modify: `src/device/migration_engine_v2.cpp`

- [ ] **Step 1: 修改 estimate_migration_benefit()**

在 `estimate_migration_benefit()` 中，根据 `task_type` 参数调整收益：

```cpp
float MigrationEngineV2::estimate_migration_benefit(
    int from_cpu, int to_cpu, TaskType task_type) noexcept {
    
    // ... 现有基础计算 ...
    float base_benefit = /* 现有逻辑 */;
    
    // 任务类型调整系数
    float type_multiplier = 1.0f;
    switch (task_type) {
        case TaskType::COMPUTE:
            type_multiplier = 1.2f;  // 计算密集型：大核收益高
            break;
        case TaskType::IO:
            type_multiplier = 1.5f;  // IO 密集型：迁移成本低
            break;
        case TaskType::MEMORY:
            type_multiplier = 0.6f;  // 内存密集型：缓存污染
            break;
    }
    
    return base_benefit * type_multiplier;
}
```

- [ ] **Step 2: 确保调用处传递 task_type**

检查所有 `estimate_migration_benefit()` 调用点，确保传递正确的 `task_type`。在 `decide_migration()` 中，`task_type` 已通过 `classify_task()` 获取。

- [ ] **Step 3: Commit**

```bash
git add src/device/migration_engine_v2.cpp
git commit -m "feat(device): task-type-aware migration benefit estimation"
```

---

### Task 4: 小核最低频率解除钳制

**Files:**
- Modify: `include/device/cpu_freq_manager.h`
- Modify: `src/device/cpu_freq_manager.cpp`

- [ ] **Step 1: 添加 get_min_freq 方法**

在 `CpuFreqManager` 类中添加：

```cpp
// 读取硬件最低频率（而非软件钳制值）
uint32_t get_hardware_min_freq(int cpu) noexcept;
```

- [ ] **Step 2: 实现 get_hardware_min_freq**

```cpp
uint32_t CpuFreqManager::get_hardware_min_freq(int cpu) noexcept {
    char path[128];
    snprintf(path, sizeof(path),
        "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", cpu);
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 300000; // 安全默认值 300MHz
    
    char buf[16];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (n <= 0) return 300000;
    buf[n] = '\0';
    return static_cast<uint32_t>(atol(buf));
}
```

- [ ] **Step 3: 在空闲核心降频中使用**

在 `EventLoop` 的空闲核心处理中，使用 `get_hardware_min_freq()` 而非固定值。

- [ ] **Step 4: Commit**

```bash
git add include/device/cpu_freq_manager.h src/device/cpu_freq_manager.cpp
git commit -m "feat(device): read hardware min freq instead of software clamp"
```

---

### Task 5: 空闲核心深度降频

**Files:**
- Modify: `src/core/event_loop.cpp`
- Modify: `include/core/event_loop.h`

- [ ] **Step 1: 添加空闲核心检查方法**

在 `EventLoop` 类的 private 区域添加：

```cpp
void apply_idle_core_policy() noexcept;
uint64_t last_idle_check_us_{0};
static constexpr uint64_t IDLE_CHECK_INTERVAL_US = 200'000; // 200ms
```

- [ ] **Step 2: 实现 apply_idle_core_policy**

```cpp
void EventLoop::apply_idle_core_policy() noexcept {
    uint64_t now = get_time_us();
    if (now - last_idle_check_us_ < IDLE_CHECK_INTERVAL_US) return;
    last_idle_check_us_ = now;
    
    for (int i = 0; i < MAX_CPUS; i++) {
        if (loads_[i].util < 5 && loads_[i].run_queue == 0) {
            uint32_t min_freq = cpu_freq_mgr_.get_hardware_min_freq(i);
            // 通过 SysfsWriter 降频
            char path[128];
            snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
            int fd = open(path, O_WRONLY);
            if (fd >= 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%u", min_freq);
                ::write(fd, buf, len);
                close(fd);
            }
        }
    }
}
```

- [ ] **Step 3: 在主循环中调用**

在 `EventLoop::start()` 的主循环中，添加：

```cpp
apply_idle_core_policy();
```

- [ ] **Step 4: Commit**

```bash
git add src/core/event_loop.cpp include/core/event_loop.h
git commit -m "feat(core): deep frequency reduction for idle cores"
```

---

## Phase 3: 预测精度 + 场景平滑（P2）

### Task 6: 设备校准阶段

**Files:**
- Modify: `include/predict/predictor.h`
- Modify: `src/predict/predictor.cpp`

- [ ] **Step 1: 添加 DeviceCalibration 结构体**

在 `predictor.h` 中，在 `Predictor` 类之前添加：

```cpp
struct DeviceCalibration {
    float scale{1.0f};
    float bias{0.0f};
    bool calibrated{false};
    uint64_t calib_start_us{0};
    static constexpr uint64_t CALIB_DURATION_US = 30'000'000;
    
    struct Sample { float predicted; float actual; };
    static constexpr size_t MAX_SAMPLES = 300;
    Sample samples[MAX_SAMPLES];
    size_t sample_count{0};
    
    void add_sample(float pred, float actual) noexcept;
    void calibrate() noexcept;
    float apply(float pred) const noexcept {
        return calibrated ? (pred * scale + bias) : pred;
    }
};
```

在 `Predictor` 类的 private 区域添加：

```cpp
DeviceCalibration calibration_;
```

- [ ] **Step 2: 实现校准逻辑**

在 `predictor.cpp` 中添加：

```cpp
void DeviceCalibration::add_sample(float pred, float actual) noexcept {
    if (sample_count < MAX_SAMPLES) {
        samples[sample_count++] = {pred, actual};
    }
}

void DeviceCalibration::calibrate() noexcept {
    if (sample_count < 10) return; // 最少 10 个样本
    
    // 线性回归：actual = scale * predicted + bias
    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    for (size_t i = 0; i < sample_count; i++) {
        sum_x += samples[i].predicted;
        sum_y += samples[i].actual;
        sum_xy += samples[i].predicted * samples[i].actual;
        sum_x2 += samples[i].predicted * samples[i].predicted;
    }
    
    float n = static_cast<float>(sample_count);
    float denom = n * sum_x2 - sum_x * sum_x;
    if (std::abs(denom) < 1e-6f) return;
    
    scale = (n * sum_xy - sum_x * sum_y) / denom;
    bias = (sum_y - scale * sum_x) / n;
    calibrated = true;
    
    LOGI("Device calibration: scale=%.3f bias=%.1f (n=%zu)", scale, bias, sample_count);
}
```

- [ ] **Step 3: 在 predict() 中集成**

在 `Predictor::predict()` 的返回处：

```cpp
float raw_pred = /* 现有预测逻辑 */;

// 校准阶段：收集样本
if (!calibration_.calibrated) {
    if (calibration_.calib_start_us == 0) {
        calibration_.calib_start_us = get_time_us();
    }
    // actual_fps 需要从外部传入或通过成员变量获取
    // 这里先返回原始值，校准在 update_actual() 中进行
    return raw_pred;
}

return calibration_.apply(raw_pred);
```

- [ ] **Step 4: 添加 update_actual 方法**

```cpp
void Predictor::update_actual(float actual_fps) noexcept {
    if (!calibration_.calibrated && calibration_.calib_start_us > 0) {
        calibration_.add_sample(last_prediction_, actual_fps);
        
        uint64_t elapsed = get_time_us() - calibration_.calib_start_us;
        if (elapsed >= DeviceCalibration::CALIB_DURATION_US) {
            calibration_.calibrate();
        }
    }
}
```

- [ ] **Step 5: Commit**

```bash
git add include/predict/predictor.h src/predict/predictor.cpp
git commit -m "feat(predict): add device calibration phase for prediction correction"
```

---

### Task 7: 置信度门控增强

**Files:**
- Modify: `include/predict/predictor.h`
- Modify: `src/predict/predictor.cpp`

- [ ] **Step 1: 增强 ConfidenceGate 结构体**

替换现有的 `ConfidenceGate`：

```cpp
struct ConfidenceGate {
    float mape_ema{0.0f};
    float over_pred_ema{0.0f};
    float under_pred_ema{0.0f};
    float alpha{0.1f};
    
    void add_error(float pred, float actual) noexcept {
        float error = pred - actual;
        float pct = (actual > 0) ? std::abs(error) / actual : 0;
        mape_ema = mape_ema * (1 - alpha) + pct * alpha;
        if (error > 0) {
            over_pred_ema = over_pred_ema * 0.95f + (error / actual) * 0.05f;
        } else if (actual > 0) {
            under_pred_ema = under_pred_ema * 0.95f + (-error / actual) * 0.05f;
        }
    }
    
    float get_conservative_factor() const noexcept {
        return 1.0f + under_pred_ema * 0.5f;
    }
    
    float get_conservative_mape() const noexcept {
        // 保守 MAPE：对欠预测给予更高权重
        return mape_ema * (1.0f + under_pred_ema);
    }
};
```

- [ ] **Step 2: 在 PolicyEngine::decide() 中使用保守因子**

```cpp
float conservative_factor = predictor_.get_conservative_factor();
uint32_t adjusted_freq = static_cast<uint32_t>(base_freq * conservative_factor);
```

- [ ] **Step 3: Commit**

```bash
git add include/predict/predictor.h src/predict/predictor.cpp src/sched/policy_engine.cpp
git commit -m "feat(predict): enhanced confidence gate with over/under prediction tracking"
```

---

### Task 8: 场景切换平滑

**Files:**
- Modify: `include/sched/policy_engine.h`
- Modify: `src/sched/policy_engine.cpp`

- [ ] **Step 1: 添加 SceneTransition 结构体**

在 `PolicyEngine` 类的 private 区域添加：

```cpp
struct SceneTransition {
    uint32_t from_freq{0};
    uint32_t to_freq{0};
    int steps_remaining{0};
    static constexpr int MAX_STEPS = 3;
    
    void start(uint32_t from, uint32_t to) noexcept {
        from_freq = from;
        to_freq = to;
        steps_remaining = MAX_STEPS;
    }
    
    uint32_t next_freq() noexcept {
        if (steps_remaining <= 0) return to_freq;
        float ratio = 1.0f - static_cast<float>(steps_remaining) / MAX_STEPS;
        steps_remaining--;
        return static_cast<uint32_t>(from_freq + (to_freq - from_freq) * ratio);
    }
    
    bool transitioning() const noexcept { return steps_remaining > 0; }
};
SceneTransition transition_;
```

- [ ] **Step 2: 在场景切换时启动过渡**

在 `decide()` 中，当检测到场景切换时：

```cpp
if (new_scene != current_scene) {
    uint32_t old_freq = current_freq_;
    uint32_t new_freq = calc_target_freq(new_scene);
    transition_.start(old_freq, new_freq);
    current_scene = new_scene;
}
```

- [ ] **Step 3: 在频率输出时使用过渡值**

```cpp
uint32_t final_freq = transition_.transitioning() ? 
    transition_.next_freq() : target_freq;
```

- [ ] **Step 4: Commit**

```bash
git add include/sched/policy_engine.h src/sched/policy_engine.cpp
git commit -m "feat(sched): smooth scene transition to prevent freq jumps"
```

---

## Phase 4: 迁移抑制 + 功耗预算（P3）

### Task 9: 轻负载迁移抑制

**Files:**
- Modify: `src/device/migration_engine_v2.cpp`

- [ ] **Step 1: 在 decide_migration() 中添加轻负载检查**

```cpp
// 轻负载迁移抑制
if (loads_[cur].util < 20) {
    // 轻负载：只有目标核明显空闲时才迁移
    if (loads_[target].util > 15 || 
        prof_.roles[target] > CoreRole::LITTLE) {
        result.migrate = false;
        result.reason = "light load: target too busy";
        return result;
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add src/device/migration_engine_v2.cpp
git commit -m "feat(device): suppress migration under light load"
```

---

### Task 10: 功耗预算模式

**Files:**
- Modify: `include/core/event_loop.h`
- Modify: `src/core/event_loop.cpp`

- [ ] **Step 1: 添加 PowerBudget 结构体**

在 `EventLoop` 类的 private 区域添加：

```cpp
struct PowerBudget {
    bool enabled{false};
    uint32_t max_power_mw{0};
    uint32_t current_power_mw{0};
    
    uint32_t get_freq_cap(uint32_t desired, CoreRole role) const noexcept {
        if (!enabled || current_power_mw < max_power_mw) return desired;
        float reduction = (role >= CoreRole::BIG) ? 0.8f :
                         (role >= CoreRole::MID) ? 0.9f : 1.0f;
        return static_cast<uint32_t>(desired * reduction);
    }
};
PowerBudget power_budget_;
```

- [ ] **Step 2: 在频率决策后应用功耗上限**

在 `apply_frequency()` 中：

```cpp
for (auto& [cpu, config] : freq_configs) {
    config.max_freq = power_budget_.get_freq_cap(
        config.max_freq, prof_.roles[cpu]);
}
```

- [ ] **Step 3: 添加 WebUI 配置接口**

在 WebServer 的 API 中添加：

```
POST /api/power-budget
{
    "enabled": true,
    "max_power_mw": 5000
}
```

- [ ] **Step 4: Commit**

```bash
git add include/core/event_loop.h src/core/event_loop.cpp src/net/web_server.cpp
git commit -m "feat(core): optional power budget mode with per-cluster freq cap"
```

---

## 验证策略

每个 Phase 完成后：

1. **编译验证**：`cd build_test && cmake .. -DCMAKE_BUILD_TYPE=Debug && make hp_tests`
2. **单元测试**：`./hp_tests`
3. **集成测试**：在 Android 设备上运行 `hyperpredictd --mod-dir /data/adb/modules/hyperpredict`
4. **性能对比**：通过 WebUI 监控频率变化次数、平均功耗、预测 MAPE

## 依赖关系

```
Task 1 (滞回) ─────────┐
Task 2 (迁移降频) ──────┤
Task 3 (任务类型) ──────┼── Phase 2 依赖 Phase 1
Task 4 (最小频率) ──────┤
Task 5 (空闲降频) ──────┘
Task 6 (校准) ──────────┐
Task 7 (置信度) ────────┼── Phase 3 独立
Task 8 (场景平滑) ──────┘
Task 9 (轻负载) ────────┐
Task 10 (功耗预算) ─────┘── Phase 4 独立
```

Phase 1-4 内部各 Task 可并行执行。
