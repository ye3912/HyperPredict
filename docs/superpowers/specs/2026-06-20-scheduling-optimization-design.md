# 调度逻辑渐进式优化设计

> 日期: 2026-06-20
> 状态: 已批准
> 方案: A — 渐进式调优

## 背景

当前调度逻辑存在 5 个主要痛点：预测不准、频率抖动、降频不及时、功耗过高、迁移时机不对。本设计在现有架构上做针对性改进，不改变核心管线结构。

## 优化范围

| 层级 | 优化项 | 优先级 |
|------|--------|--------|
| 预测层 | 设备校准阶段、置信度门控增强 | P2 |
| 策略层 | 频率滞回、场景切换平滑 | P0/P2 |
| 迁移层 | 任务类型感知、轻负载抑制、迁移后源核降频 | P0/P1/P3 |
| 功耗层 | 小核最低频率解除、空闲核心降频、功耗预算 | P1/P1/P3 |

## 一、预测层优化

### 1.1 设备校准阶段（P2，~150 行）

**目标**：用设备特定数据修正通用模型的预测偏差。

**机制**：
- 守护进程启动后前 30 秒为校准模式
- 校准期间记录 `(predicted_fps, actual_fps)` 数据对
- 校准结束后用最小二乘法计算线性修正系数：`corrected = raw_pred * scale + bias`
- 修正系数持久化到 `/data/adb/modules/hyperpredict/calibration.json`

**数据结构**：
```cpp
struct DeviceCalibration {
    float scale{1.0f};
    float bias{0.0f};
    bool calibrated{false};
    uint64_t calib_start_us{0};
    static constexpr uint64_t CALIB_DURATION_US = 30'000'000;
    
    struct Sample { float predicted; float actual; };
    std::vector<Sample> samples;
    static constexpr size_t MAX_SAMPLES = 300; // 10Hz * 30s
    
    void add_sample(float pred, float actual) noexcept;
    void calibrate() noexcept;  // 计算 scale/bias
    float apply(float pred) const noexcept { return pred * scale + bias; }
};
```

**集成点**：`Predictor::predict()` 返回前调用 `calibration_.apply(raw_pred)`。

### 1.2 置信度门控增强（P2，~80 行）

**目标**：区分过预测（浪费功耗）和欠预测（导致掉帧），采用保守策略。

**改动**：
```cpp
struct ConfidenceGate {
    float mape_ema{0.0f};
    float over_pred_ema{0.0f};   // pred > actual
    float under_pred_ema{0.0f};  // pred < actual
    float alpha{0.1f};
    
    void add_error(float pred, float actual) noexcept {
        float error = pred - actual;
        float pct = actual > 0 ? std::abs(error) / actual : 0;
        mape_ema = mape_ema * (1 - alpha) + pct * alpha;
        if (error > 0) {
            over_pred_ema = over_pred_ema * 0.95f + (error / actual) * 0.05f;
        } else {
            under_pred_ema = under_pred_ema * 0.95f + (-error / actual) * 0.05f;
        }
    }
    
    // 保守因子：欠预测越多，越倾向于给更高频率
    float get_conservative_factor() const noexcept {
        return 1.0f + under_pred_ema * 0.5f;
    }
};
```

**集成点**：`PolicyEngine::decide()` 中，最终频率乘以 `conservative_factor`。

## 二、策略层优化

### 2.1 频率滞回机制（P0，~100 行）

**目标**：消除场景边界附近的频率抖动。

**参数**：
```cpp
struct FreqHysteresis {
    static constexpr float UP_THRESHOLD = 0.92f;    // 升频阈值：预测 < 目标 * 0.92
    static constexpr float DOWN_THRESHOLD = 1.05f;  // 降频阈值：预测 > 目标 * 1.05
    static constexpr int DOWN_COUNT = 5;             // 降频需连续 5 次满足条件
    int down_counter{0};
};
```

**PolicyEngine::decide() 改动**：
```cpp
float ratio = predicted_fps / target_fps;

if (ratio < UP_THRESHOLD) {
    // 快速升频
    hysteresis_.down_counter = 0;
    freq_mode_ = FreqMode::PERFORMANCE;
} else if (ratio > DOWN_THRESHOLD) {
    // 延迟降频
    hysteresis_.down_counter++;
    if (hysteresis_.down_counter >= DOWN_COUNT) {
        freq_mode_ = FreqMode::BALANCED;
        hysteresis_.down_counter = 0;
    }
} else {
    // 滞回区间：保持当前模式
    hysteresis_.down_counter = 0;
}
```

### 2.2 场景切换平滑（P2，~100 行）

**目标**：场景切换时频率渐进过渡，避免跳变。

**机制**：
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
        return from_freq + (to_freq - from_freq) * ratio;
    }
    
    bool transitioning() const noexcept { return steps_remaining > 0; }
};
```

**集成点**：`PolicyEngine::decide()` 中，场景切换时调用 `transition_.start()`，后续调用 `transition_.next_freq()`。

## 三、迁移层优化

### 3.1 任务类型感知迁移（P1，~80 行）

**目标**：根据任务类型调整迁移收益估算。

**MigrationEngineV2::estimate_migration_benefit() 改动**：
```cpp
float benefit = base_benefit;

switch (task_type) {
    case TaskType::COMPUTE:
        benefit *= 1.2f;  // 计算密集型：迁移到大核收益高
        break;
    case TaskType::IO:
        benefit *= 1.5f;  // IO 密集型：迁移成本低，收益高
        break;
    case TaskType::MEMORY:
        benefit *= 0.6f;  // 内存密集型：缓存污染成本高
        break;
}
```

### 3.2 轻负载迁移抑制（P3，~40 行）

**目标**：减少轻负载时的无效迁移。

**条件**：源核 util < 20 且目标核 util > 15 时，跳过迁移。

### 3.3 迁移后源核降频（P0，~50 行）

**目标**：任务迁移后立即降低源核频率。

**机制**：`migrate()` 成功后，计算源核当前负载对应的频率并写入 sysfs。

## 四、功耗优化

### 4.1 小核最低频率解除钳制（P1，~30 行）

**目标**：允许小核降至硬件最低频率。

**改动**：`CpuFreqManager::get_min_freq()` 读取 `cpuinfo_min_freq` 而非 `scaling_min_freq`。

### 4.2 空闲核心深度降频（P1，~50 行）

**目标**：空闲核心降至最低频率。

**条件**：util < 5 且 run_queue == 0 的核心。

**集成点**：`EventLoop` 主循环中，每 200ms 检查一次空闲核心。

### 4.3 功耗预算模式（P3，~120 行）

**目标**：可选的全局功耗约束。

**机制**：
```cpp
struct PowerBudget {
    bool enabled{false};
    uint32_t max_power_mw{0};
    uint32_t current_power_mw{0};
    
    uint32_t get_freq_cap(uint32_t desired, CoreRole role) const noexcept {
        if (!enabled || current_power_mw < max_power_mw) return desired;
        float reduction = (role >= CoreRole::BIG) ? 0.8f :
                         (role >= CoreRole::MID) ? 0.9f : 1.0f;
        return desired * reduction;
    }
};
```

**配置**：通过 WebUI 或 JSON 配置文件启用。

## 五、实现优先级

| 阶段 | 优化项 | 预期效果 | 改动量 | 验证方式 |
|------|--------|---------|--------|---------|
| Phase 1 | 频率滞回 (2.1) | 消除抖动 | ~100 行 | 对比频率变化次数 |
| Phase 1 | 迁移后源核降频 (3.3) | 降低功耗 | ~50 行 | 测量 idle 功耗 |
| Phase 2 | 任务类型感知 (3.1) | 改善迁移 | ~80 行 | 对比迁移成功率 |
| Phase 2 | 小核最低频率 (4.1) | 降低功耗 | ~30 行 | 测量待机功耗 |
| Phase 2 | 空闲核心降频 (4.2) | 降低功耗 | ~50 行 | 测量 idle 功耗 |
| Phase 3 | 设备校准 (1.1) | 提高精度 | ~150 行 | 对比 MAPE |
| Phase 3 | 置信度门控 (1.2) | 减少过预测 | ~80 行 | 对比 over_pred_ema |
| Phase 3 | 场景切换平滑 (2.2) | 减少跳变 | ~100 行 | 对比频率方差 |
| Phase 4 | 轻负载抑制 (3.2) | 减少迁移 | ~40 行 | 对比迁移次数 |
| Phase 4 | 功耗预算 (4.3) | 全局约束 | ~120 行 | WebUI 配置测试 |

**总改动量**：~800 行

## 六、风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 滞回参数过激进 | 响应变慢 | 提供可调参数，通过 WebUI 暴露 |
| 校准数据不足 | 修正系数不准 | 设置最小样本数阈值，不足时使用默认值 |
| 迁移后源核降频过快 | 任务回迁 | 降频前检查源核是否仍有任务 |
| 功耗预算过低 | 性能严重下降 | 设置最低性能保障 |
