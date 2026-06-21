# HyperPredict 架构优化计划

> 基于 Linus 五层审查 + cpp-ultra 标准 + 架构深度分析
> 创建日期：2026-06-21
> 分支：beta

## 审查方法论

使用 gstack linus-review 技能的五层分析法：
- **Layer 1**：数据结构分析 — 数据关系、所有权、缓存友好性
- **Layer 2**：特殊情况分析 — 分支、魔法数字、死代码路径
- **Layer 3**：复杂度分析 — 函数长度、嵌套深度、认知负荷
- **Layer 4**：抽象审计 — AHA 原则、未使用代码、错误抽象

辅以架构级分析：
- 模块依赖图
- 热路径数据流追踪
- 状态所有权审计

---

## 审查结果总览

| Layer | GARBAGE | BAD TASTE | MEH | GOOD TASTE |
|-------|---------|-----------|-----|------------|
| Layer 1: 数据结构 | 3 | 7 | 4 | 5 |
| Layer 2: 特殊情况 | ~85 | ~15 | ~3 | ~8 |
| Layer 3: 复杂度 | 6 | 12 | 7 | 8 |
| Layer 4: 抽象审计 | 12 | 6 | 6 | 6 |
| **合计** | **~106** | **~40** | **~20** | **~27** |

---

## 关键架构发现

### 1. EMA 六重复制（CRITICAL）

同一个 `cpu_util` 在 6 个地方独立做 EMA 平滑：

| 位置 | 字段 | Alpha | 状态 |
|------|------|-------|------|
| Predictor::multi_scale_ | util_10ms/50ms/200ms/500ms | 0.7/0.3/0.1/0.05 | 活跃 |
| PolicyEngine::Impl | ewma_util_short/medium/long | 不同值 | 活跃 |
| CooperativeScheduler | cores_[i].util | 7/8+1/8 | 未调用 |
| MigrationEngineV2 | util_history_[] | 圆形缓冲区 | 活跃 |
| ParallelPredictor | parallel_state_ | CAS | 死代码 |
| PolicyEngine | pred_state_.ewma_util | — | 死字段 |

**影响**：调优一个组件会导致其他组件不同步。

### 2. 双重场景检测（HIGH）

两条独立路径可能产生不同结果：

- **Path A**：`SceneClassifier::classify()` → `predictor_.get_current_scene()`
- **Path B**：`EventLoop::is_gaming_scene()` → 包名 + 启发式

`process()` 的 game/non-game 分支用 Path B，但传给 PolicyEngine 的 scene 用 Path A。两者可以矛盾。

### 3. 双优化器冲突（HIGH — 正确性问题）

- `neural_.train()` 用 SGD 更新所有层权重
- `neural_.ftrl().online_update()` 用 FTRL 更新同一组权重
- 但 FTRL 维护的 `online_weights_[]` **从未被** `neural_.predict()` 读取
- FTRL 计算是纯开销，对预测零影响

### 4. IO-Wait 三重复制（HIGH）

| 位置 | 范围 | 衰减 |
|------|------|------|
| Predictor::IoWaitBoostManager | 0-1024 | 50ms |
| PolicyEngine::Impl::io_wait_boost_ | 0-192 | 7/8 |
| MultiScaleFeatures | flag + value | — |

EventLoop 写入 Predictor 的管理器，但 **从不调用** PolicyEngine 的 `set_io_wait_boost()`。

### 5. LockFreeQueue 不必要（MEDIUM）

`collect()` 和 `process()` 在同一线程中运行，队列添加了 CAS 开销 + 3 次 120 字节复制，零收益。

### 6. 大量死状态（MEDIUM）

| 字段 | 位置 | 状态 |
|------|------|------|
| `freq_mode_` | EventLoop | 永远 POWERSAVE，从未写入 |
| `io_wait_detected_` | EventLoop | 递增但从未读取 |
| `pred_state_` | PolicyEngine | init 后从未更新或读取 |
| `hist_[3]` | PolicyEngine | init 后从未更新或读取 |
| `core_capacities_[8]` | CoreBinder | 初始化但从未读取 |
| `MARGIN_*` 常量 | EventLoop | 只被死代码使用 |

---

## 用户决策

| 问题 | 决策 |
|------|------|
| 并行框架 | 完整并行化（SIMD + 并行训练 + 并行迁移评估 + 负载感知线程池） |
| CooperativeScheduler | 提取游戏/前台/预测调度逻辑到 MigrationEngineV2 |
| FreqMapTable | 保留，修复初始化，标记 TODO |
| SIMDMatrix | 保留，整合到 NeuralPredictor |
| AllBigConfig | 整合到 MigrationEngineV2 的 all-big 路径 |
| EMA 统一 | 创建 FeatureSmoother 共享类 |
| 场景检测 | 统一到 Predictor 的 SceneClassifier |
| FTRL 处理 | 分层优化（SGD 隐藏层，FTRL 输出层） |

---

## 优化计划

### Phase 1: 架构级修复（P0，最高优先级）

#### Task 1A: 创建 FeatureSmoother — 统一 EMA

**问题**：cpu_util 在 6 个地方独立做 EMA 平滑。

**方案**：
- 创建 `include/core/feature_smoother.h`
- `FeatureSmoother` 类维护 4 窗口 EMA（10ms/50ms/200ms/500ms）
- Predictor 和 PolicyEngine 都从 FeatureSmoother 读取平滑值
- 删除 PolicyEngine::Impl 中的 `ewma_util_short_/medium_/long_` 和 `ewma_fps_short_/long_`
- 删除 Predictor::MultiScaleFeatures 中的 `util_10ms/50ms/200ms/500ms` 和 `fps_10ms/50ms/200ms`
- FeatureSmoother 由 EventLoop 拥有，Predictor 和 PolicyEngine 持有 const ref

**文件变更**：
- 新建：`include/core/feature_smoother.h`
- 修改：`include/core/event_loop.h` — 拥有 FeatureSmoother 实例
- 修改：`include/predict/predictor.h` — 删除 MultiScaleFeatures 中的 EMA 字段
- 修改：`src/predict/predictor.cpp` — 从 FeatureSmoother 读取
- 修改：`src/sched/policy_engine.cpp` — 从 FeatureSmoother 读取

#### Task 1B: 消除双重场景检测

**问题**：EventLoop::is_gaming_scene() 和 Predictor 的 SceneClassifier 可能产生不同结果。

**方案**：
- 删除 `EventLoop::is_gaming_scene()` 函数
- `process()` 中的 game/non-game 分支改为使用 `predictor_.get_current_scene()`
- 判断条件：`scene == SchedScene::HEAVY || scene == SchedScene::BOOST` 走游戏路径

**文件变更**：
- 修改：`src/core/event_loop.cpp` — 删除 is_gaming_scene()，修改 process() 分支
- 修改：`include/core/event_loop.h` — 删除 is_gaming_scene() 声明

#### Task 1C: 修复双优化器（SGD + FTRL）

**问题**：SGD 和 FTRL 同时更新同一组权重，但 FTRL 的 online_weights_ 从未被 predict() 读取。

**方案**（分层优化）：
- SGD 用于隐藏层（hidden1→hidden2 的 128+128 权重）
- FTRL 用于输出层（hidden2→output 的 8 权重）
- 修改 `NeuralPredictor::train()`：只用 SGD 更新隐藏层
- 修改 `Predictor::train()`：FTRL 只更新输出层梯度
- `NeuralPredictor::predict()` 使用 SGD weights 做隐藏层推理，FTRL online_weights_ 做输出层推理

**文件变更**：
- 修改：`src/predict/predictor.cpp` — train() 和 predict() 分层逻辑
- 修改：`include/predict/predictor.h` — 添加输出层 FTRL 权重访问器

#### Task 1D: 替换 LockFreeQueue 为直接调用 + 合并 /proc/stat

**问题 1**：LockFreeQueue 在同一线程中添加 CAS 开销 + 3 次 120B 复制。
**问题 2**：read_cpu_util() 和 read_wakeups() 都打开 /proc/stat。

**方案**：
- 将 `queue_.try_push(f)` + `queue_.try_pop()` 替换为直接成员变量赋值
- 保留 queue 声明（可能未来需要跨线程），但当前用 `current_feature_` 直接传递
- 合并 `/proc/stat` 读取：一次 pread 读取整个文件，同时解析 cpu_util 和 ctxt

**文件变更**：
- 修改：`src/core/event_loop.cpp` — 直接传递，不用队列
- 修改：`include/core/event_loop.h` — 添加 current_feature_ 成员
- 修改：`src/core/system_collector.cpp` — 合并 /proc/stat 读取

#### Task 1E: 删除死状态 + 统一 FreqMode

**要删除的死状态**：
- `EventLoop::freq_mode_` + `get_freq_margin()` + `MARGIN_*` 常量
- `EventLoop::io_wait_detected_`
- `PolicyEngine::pred_state_`（6 个字段）
- `PolicyEngine::hist_[3]`
- `CoreBinder::core_capacities_[8]` + `init_capacity()`

**FreqMode 统一**：
- 删除 `EventLoop::FreqMode` 私有枚举
- 统一使用 `sched::FreqMode`

**文件变更**：
- 修改：`include/core/event_loop.h` — 删除死字段和死方法
- 修改：`include/sched/policy_engine.h` — 删除 pred_state_、hist_
- 修改：`include/device/core_binder.h` — 删除 core_capacities_

---

### Phase 2: SIMDMatrix 整合

#### Task 2.1: NEON 加速 NeuralPredictor 前向传播

**方案**：
- 调查 SIMDMatrix 的 matvec_mul 是否适合 8→16→8→1 的小矩阵
- 如果 4x4 tiled 不适合，改用简单的 NEON vmlaq/vaddq 逐行计算
- 修复权重跨步问题
- 在 `NeuralPredictor::predict()` 中用 `#ifdef __aarch64__` 启用 NEON

**文件变更**：
- 修改：`include/core/parallel.h` — 可能需要添加小矩阵 NEON 路径
- 修改：`src/predict/predictor.cpp` — 启用 NEON 前向传播

---

### Phase 3: 并行化

#### Task 3.1: 并行预测（线性 + 神经网络同时运行）

**方案**：
```
predict():
    future<float> linear_fut = pool.enqueue([this]{ return predict_linear(); });
    future<float> neural_fut = pool.enqueue([this]{ return neural_.predict(); });
    float linear = linear_fut.get();
    float neural = neural_fut.get();
    return linear * w1 + neural * w2;
```

**注意**：ThreadPool 最小 2 线程，AsyncTrainer 用 1 线程。需要增加线程池大小。

#### Task 3.2: 并行迁移评估（8 核 EDP 并行计算）

**方案**：
```
std::array<uint32_t, 8> edp_scores;
parallel_for(0, 8, [&](int i) {
    edp_scores[i] = calc_core_edp(i, ...);
});
int best = std::min_element(edp_scores.begin(), edp_scores.end());
```

#### Task 3.3: 负载感知线程池

**方案**：
- 系统负载低（cpu_util < 300）→ 1 线程（省电）
- 系统负载中等（300-700）→ 2 线程
- 系统负载高（> 700）→ 4 线程（最大吞吐）

---

### Phase 4: CooperativeScheduler → MigrationEngineV2

#### Task 4.1: 提取游戏模式逻辑

从 `CooperativeScheduler::decide_game_mode()` 提取：
- 优先大核、反向扫描
- run_queue 阈值（< 4）
- 冷却期机制

整合到 `MigrationEngineV2::decide()` 的游戏模式分支。

#### Task 4.2: 提取前台模式逻辑

从 `CooperativeScheduler::decide_foreground_mode()` 提取：
- 前台应用优先调度
- uclamp 提升策略

#### Task 4.3: 提取预测调度逻辑

从 `CooperativeScheduler::decide_predictive()` 提取：
- 基于趋势的预迁移（在负载升高前就迁移）
- 利用 MigrationEngineV2 已有的 TrendData

#### Task 4.4: 删除 CoreBinder 空壳方法

删除 `CoreBinder::apply()`、`bind_sched()`、`adjust_binding()`、`mode()` 空存根。

---

### Phase 5: 数据结构修复

#### Task 5.1: NeuralPredictor 偏置 vector→array

- `vector<vector<float>>` → `std::array<float, 16> + std::array<float, 8> + float`
- 消除热路径上的两次堆指针间接寻址

#### Task 5.2: LoadFeature 热/冷分离

- 拆分为 `LoadFeatureHot`（28 字节：7 个数值字段）和 `LoadFeatureMeta`（package_name 等）
- 减少每周期复制的数据量

#### Task 5.3: DeviceCalibration 生命周期

- 校准完成后释放 2.4KB 样本数组
- 修复 MAX_SAMPLES 与 CALIB_DURATION_US 的不一致

#### Task 5.4: FreqMapTable 修复初始化

- 从 `FreqDomain::steps` 初始化实际频率表
- 标记 TODO：如果 PolicyEngine 的公式计算效果更好，FreqMapTable 可以作为备选

#### Task 5.5: AllBigConfig 整合

- 将 `AllBigConfig` 的配置项接入 `decide()` 第 6 节的 all-big 路径
- `detect_device_generation()` 中的全大核检测应该设置 AllBigConfig

---

### Phase 6: 统一魔法数字

#### Task 6.1: 创建 `include/core/sched_constants.h`

统一热阈值（当前有三套不同方案）：
```cpp
namespace hp::sched {
    // 热阈值
    inline constexpr int kThermalEmergency = 5;
    inline constexpr int kThermalCritical = 8;
    inline constexpr int kThermalWarning = 12;
    inline constexpr int kThermalModerate = 20;
    inline constexpr int kThermalHigh = 30;
    inline constexpr int kThermalSevere = 40;

    // 负载阈值（0-1024 范围）
    inline constexpr uint32_t kOverutilThreshold = 870;
    inline constexpr uint32_t kLightLoadThreshold = 20;
    inline constexpr uint32_t kVeryIdleThreshold = 5;
    inline constexpr uint32_t kIdleLoadThreshold = 51;

    // 频率增益
    inline constexpr int32_t kFreqGainPerFpsError = 12000;
    inline constexpr int32_t kMaxFreqDelta = 180000;
    inline constexpr int32_t kDeadZoneThreshold = 35000;

    // EMA alpha
    inline constexpr float kEmaAlphaShort = 0.30f;
    inline constexpr float kEmaAlphaMedium = 0.50f;
    inline constexpr float kEmaAlphaLong = 0.70f;

    // 学习率
    inline constexpr float kLrIdle = 0.002f;
    inline constexpr float kLrLight = 0.005f;
    inline constexpr float kLrMedium = 0.008f;
    inline constexpr float kLrVideo = 0.006f;
    inline constexpr float kLrHeavy = 0.015f;
    inline constexpr float kLrBoost = 0.020f;
    inline constexpr float kLrIoWait = 0.010f;

    // FPS
    inline constexpr float kMaxFps = 144.0f;
    inline constexpr float kDefaultFps = 60.0f;
}
```

#### Task 6.2-6.6: 替换各文件中的裸数字

- `event_loop.cpp`（~30 个）
- `policy_engine.cpp`（~25 个）
- `migration_engine_v2.cpp`（~20 个）
- `predictor.cpp`（~15 个）
- `hardware_analyzer.cpp`（~10 个）

---

### Phase 7: 拆分大函数 + 其他优化

#### Task 7.1: 拆分 `apply_freq_config()`（145 行）

提取：
- `apply_uclamp_cgroup_v2()` — uclamp cgroup v2 回退逻辑
- `write_freq_cached()` — 带脏检查的频率写入（统一 4 处重复模式）
- `write_uclamp_cached()` — 带脏检查的 uclamp 写入
- 修复大括号缩进问题

#### Task 7.2: 拆分 `PolicyEngine::decide()`（278 行）

提取：
- `update_ema()` — 多尺度 EMA 更新（如果 Phase 1A 未完成）
- `calculate_trend()` — 趋势/加速度计算
- `apply_io_boost()` — IO-Wait 提频
- `apply_touch_boost()` — 触摸提频
- `apply_trend_correction()` — 趋势修正
- `apply_thermal_scaling()` — 热缩放
- `apply_uclamp_settings()` — UClamp 设置

#### Task 7.3: 拆分 `process()`（176 行）

提取：
- `execute_migration()` — 迁移执行（affinity mask 构建）
- 将 game/non-game 分支拆分为独立函数

#### Task 7.4: 传递 domain_idx 到 apply_freq_config

- `process()` 已经用 O(1) 查表计算了 domain_idx
- `apply_freq_config()` 内部又用 O(n) 线性扫描重新计算
- 添加 `int domain_idx` 参数

#### Task 7.5: 统一 IO-Wait 源

- 删除 PolicyEngine 的独立 IO-Wait 检测
- EventLoop 从 Predictor 的 IoWaitBoostManager 获取 boost 值
- 传递给 PolicyEngine::decide() 作为参数

---

### Phase 8: 解耦头文件

#### Task 8.1: 提取 SchedScene 到 predict/scenes.h

- `policy_engine.h` 只需要 `SchedScene` 枚举，但 include 了整个 `predictor.h`
- 提取 `SchedScene` 到独立头文件

#### Task 8.2: HardwareProfile 改用 const ref

- MigrationEngineV2、CooperativeScheduler 各持有一份 HardwareProfile 副本
- 改为 `const HardwareProfile&` 引用

#### Task 8.3: 修复 TargetFPS 双枚举

- `hp::TargetFPS` 和 `hp::device::TargetFPS` 是两个不兼容的类型
- 合并为一个

---

## 修正的错误（Phase 1-6 修复 + Tasks 1-10 优化中引入的）

| 文件 | 错误 | 修复 |
|------|------|------|
| `system_collector.cpp:80` | `std::call_once` 无法捕获 static 局部变量 | 改回 `if (!pacer_inited)` |
| `cpu_topology.cpp:47,49` | `-fno-exceptions` 下不能用 try/catch | 改用 `strtoul()` |
| `event_loop.cpp:547` | 三元运算符类型不匹配 | `static_cast<int>` 移到 CoreRole 侧 |
| `predictor.cpp:867` | `get_weights()` 签名不匹配 | 添加 `weights_data()` 访问器 |

---

## 预计总工作量

| Phase | 预计时间 | 影响 |
|-------|---------|------|
| Phase 1: 架构级修复 | ~8 小时 | 消除 6 重 EMA、双重场景检测、双优化器冲突、死状态 |
| Phase 2: SIMD 加速 | ~3 小时 | ARM64 推理性能提升 |
| Phase 3: 并行化 | ~5 小时 | 多核利用 |
| Phase 4: CooperativeScheduler 整合 | ~4 小时 | 更先进的调度逻辑 |
| Phase 5: 数据结构修复 | ~3 小时 | 热路径性能、消除 UB |
| Phase 6: 魔法数字统一 | ~4 小时 | 可维护性 |
| Phase 7: 拆分函数 + 其他 | ~4 小时 | 可读性 |
| Phase 8: 解耦头文件 | ~2 小时 | 编译速度、模块化 |
| **总计** | **~33 小时** | |

---

## 做得好的地方（GOOD TASTE）

- `FreqFdCache` 脏检查模式避免冗余 sysfs 写入
- `LockFreeQueue` 用于 collect→process 管道（虽然当前不必要）
- `shared_mutex` 用于单写多读
- `FTRLLearner` 使用扁平 C 数组，缓存友好
- `SceneClassifier::classify()` 干净的优先级级联
- `DeviceCalibration::calibrate()` 标准 OLS 回归
- `calc_total_edp()` NEON SIMD 路径带标量回退
- `build_cpu_domain_map()` O(1) 查找表
- `detect_sched_backend()` 清晰的级联探测
