# HyperPredict Beta 分支 — 代码质量修复计划

> 审查日期: 2026-06-20
> 总问题数: 78 (Critical 9, High 25, Medium 33, Low 17)

## Phase 1: Critical UB / 内存安全（9 个）

| # | 文件 | 行号 | 问题 | 状态 |
|---|------|------|------|------|
| 1.1 | `src/core/event_loop.cpp` | 288 | `domain_idx == -1` 越界访问 `domains[-1]` | ✅ |
| 1.2 | `src/core/logger.cpp` | 156,160 | `va_list` 被两个函数重复消费，ARM 上 UB | ✅ |
| 1.3 | `src/core/frame_pacer.cpp` | 81-110 | `fork()` 失败时 pipe fd 泄漏 | ✅ |
| 1.4 | `include/core/task_decomposer.h` | 310-313 | `parallel_map` 当 `start > 0` 时越界写入 | ✅ |
| 1.5 | `include/core/load_aware_pool.h` | 233-246 | `shrink_pool()` 死锁 — worker 永远不被唤醒 | ✅ |
| 1.6 | `src/net/web_server.cpp` | 122-128 | SHA1 Base64 编码越界读 `digest[20]` | ✅ |
| 1.7 | `src/net/web_server.cpp` | 557-567 | WebSocket 帧大小用已解码长度，缓冲区错位 | ✅ |
| 1.8 | `src/device/migration_engine_v2.cpp` | 583-589 | NEON mask `0xFFFFFFFF` 转 float = 4.29e9 而非 1.0 | ✅ |
| 1.9 | `src/device/migration_engine_v2.cpp` | 550-554 | `prof_.freqs[]` 永远为 0，频率感知模型失效 | ✅ |

## Phase 2: Critical 逻辑错误（5 个）

| # | 文件 | 行号 | 问题 | 状态 |
|---|------|------|------|------|
| 2.1 | `src/device/hardware_analyzer.cpp` | 259 | `rank++` 在循环外，所有核心被标记为 PRIME | ✅ |
| 2.2 | `include/core/parallel.h` | 104-136 | NEON `matvec_mul` 逻辑完全错误 | ✅ |
| 2.3 | `include/core/parallel_compute.h` | 148-165 | 输出层也应用了 ReLU，回归任务截断负值 | ✅ |
| 2.4 | `src/predict/predictor.cpp` | 723 | 线性模型误差 = FPS - 毫秒，单位混杂 | ✅ |
| 2.5 | `src/predict/predictor.cpp` | 358-373 | `train_multi_scale()` 缺少隐藏层反向传播 | ✅ |

## Phase 3: High 线程安全（6 个）

| # | 文件 | 行号 | 问题 | 状态 |
|---|------|------|------|------|
| 3.1 | `src/core/logger.cpp` | 37-42 | 全局日志状态无线程同步 | ✅ |
| 3.2 | `src/core/system_collector.cpp` | 78-83 | 双重检查锁定 `inited` 非原子 | ✅ |
| 3.3 | `include/cache/lru_cache.h` | 56-71 | TOCTOU：shared_lock→unlock→unique_lock | ✅ |
| 3.4 | `src/predict/predictor.cpp` | 752-760 | 异步训练与预测并发访问权重 | ✅ |
| 3.5 | `include/device/parallel_migration.h` | 67-68 | TOCTOU 原子 flag | ✅ |
| 3.6 | `include/core/load_aware_pool.h` | 371-372 | `workers_.size()` 无锁读 | ✅ |

## Phase 4: High 逻辑/数值错误（13 个）

| # | 文件 | 行号 | 问题 | 状态 |
|---|------|------|------|------|
| 4.1 | `src/core/system_collector.cpp` | 240 | `uint32_t` 截断 ctxt 计数器 | ✅ |
| 4.2 | `src/core/event_loop.cpp` | 1057-1064 | uclamp sysfs 路径错误 | ✅ |
| 4.3 | `src/core/event_loop.cpp` | 440-443 | Rate limiting 计算了但从未执行 | ✅ |
| 4.4 | `src/core/event_loop.cpp` | 185 | `frame_interval_us == 0` 时除零 | ✅ |
| 4.5 | `include/core/parallel_compute.h` | 263 | `8 - run_queues[i]` 无符号下溢 | ✅ |
| 4.6 | `src/core/event_loop.cpp` | 834-836 | cgroup v1 检测后设置为 V2 枚举 | ✅ |
| 4.7 | `src/core/boot_calibrator.cpp` | — | `baseline_.mid` 从未校准 | ✅ |
| 4.8 | `include/core/types.h` | 60-61,93-96 | 原神识别为 60fps 所以 `is_game` 返回 false | ✅ |
| 4.9 | `src/predict/predictor.cpp` | 739-746 | FTRL 梯度只覆盖 8/264 个权重 | ✅ |
| 4.10 | `src/net/web_server.cpp` | 478 | WebSocket `recv_buf` 无上限 | ✅ |
| 4.11 | `src/net/web_server.cpp` | 853-860 | `broadcast()` 持锁阻塞发送 | ✅ |
| 4.12 | `src/net/web_server.cpp` | 847-851 | `send()` 返回值被忽略 | ✅ |
| 4.13 | `include/device/core_binder.h` | 337-411 | 重复 `cores_` 数组 | ✅ |

## Phase 5: Medium 严重度（33 个）

### 5a — predict 模块（7 个）
- `predictor.cpp:614` — 趋势斜率用了已更新的 EMA
- `predictor.cpp:297` — `actual_fps == 0` 时除零
- `predictor.cpp:769` — `export_model()` memcpy 无边界检查
- `predictor.cpp:22` — `rand()` 非线程安全
- `parallel_predictor.h:66-99` — 原子 flag 无异常安全
- `parallel_predictor.h:53-54` — 调用另一个类的 private 方法
- `fallback_manager.cpp:61-63` — static idx 跨实例共享

### 5b — sched 模块（4 个）
- `policy_engine.cpp:275-284` — EMA 趋势 bug
- `policy_engine.cpp:318-320` — `set_freq_mode()` 被 `decide()` 覆盖
- `policy_engine.cpp:459-461` — config_hash 死代码
- `policy_engine.cpp:184-191` — 空 FreqMapTable 初始化

### 5c — device 模块（6 个）
- `cpu_topology.cpp:47-49` — `std::stoul` 在 noexcept 中可能抛异常
- `soc_database.cpp:346-349` — SoC codename 大小写不匹配
- `migration_engine_v2.cpp:284-286` — `estimate_power_savings` 无边界检查
- `migration_engine_v2.cpp:604-606` — `check_capacity` 核心数为 0 时失效
- `energy_model.cpp:58-63` — `target_fps == 0` 时除零
- `soc_database.cpp:450` — `getAppTargetFps` 无头文件声明

### 5d — net 模块（3 个）
- `web_server.cpp:235-262` — `nn_weights` 内部维度无边界检查
- `web_server.cpp:172` — `%lu` 用于 `uint64_t`
- `web_server.cpp:881-884` — `const_cast` 应改为 `mutable`

### 5e — core 模块（8 个）
- `event_loop.cpp:23` — `RATE_LIMIT_MIN_US` 头文件和源文件值冲突
- `parallel.h:283` — 默认参数在参数包后
- `parallel.h:297-300` — `wait_all()` 忙等待
- `parallel.h:413` — `AsyncTrainer` 捕获可能悬垂引用
- `parallel_compute.h:368-376` — 空数组 min/max UB
- `load_aware_pool.h:284-285` — `std::result_of_t` C++20 已移除
- `sysfs_writer.cpp:12` — 硬编码 8 而非 `MAX_CPUS`
- `sysfs_writer.cpp:46` — 同上

## Phase 6: Low 严重度（17 个）✅

| # | 文件 | 问题 | 状态 |
|---|------|------|------|
| 6.1 | `event_loop.cpp:203` | `frame_error_ema` 死变量 | ✅ 已移除 |
| 6.2 | `frame_pacer.cpp:29` | `sf_buffer_` 分配但从未使用 | ✅ 设为 nullptr |
| 6.3 | `migration_engine_v2.cpp:355` | `power_mw` 死变量 | ✅ 已移除 |
| 6.4 | `soc_database.cpp:402,436` | 重复 map entry `codm` | ✅ 已移除重复项 |
| 6.5 | `sysfs_writer.cpp:136` | `detect_cg_root()` 从未调用 | ✅ 已从 .h 和 .cpp 移除 |
| 6.6 | `system_collector.h:65-94` | 13 个只有声明没有定义的函数 | ✅ 已移除未实现声明 |
| 6.7 | `types.h:53` | `constexpr strstr` 非法 | ✅ 移除 constexpr |
| 6.8 | `core_binder.h:43` | `const char*` 应为 `string_view` | ✅ 改为 `std::string_view` |
| 6.9 | `fallback_manager.cpp:98` | `hist_` 未在 `reset()` 中清零 | ✅ 已添加 |
| 6.10 | `main.cpp:8` | 信号处理器调用非信号安全函数 | ✅ 改为 `sig_atomic_t` flag + 监控线程 |

> 注: `config_hash` (5b.3)、`static constexpr` (5e.1)、`const_cast` (5d.3)、空 FreqMapTable (5b.4) 已在 Phase 5 中修复。

## 关键依赖

1. Phase 1.9 (`prof_.freqs[]`) → 必须在 Phase 4 EDP 相关修复之前
2. Phase 2.1 (`rank++`) → 必须在 Phase 4.13 (`cores_` 重复) 之前
3. Phase 2.5 (`train_multi_scale`) → 必须在 Phase 4.9 (FTRL 梯度) 之前
