# 调度优化需求单：Snapdragon 8 Elite / LOL 手游稳帧降功耗

> 创建时间：2026-04-22
> 目标设备：Xiaomi 13 (SM8650)

## 核心目标
- **帧率**：保持 `AVG 60±1 FPS`，`5% Low ≥ 58 FPS`
- **功耗**：平均功耗从 `3.37W` 降至 `2.6~2.9W`（↓15~20%）
- **体验**：消除 `FrameTime` 尖峰，降低频率锯齿波动

## 代码级优化清单

### 1. event_loop.cpp

| 函数/位置 | 当前逻辑 | 优化建议 |
|:---|:---|:---|
| `calculate_fas_delta` | `MARGIN_FPS[] = {1,1,2,3}`, `deadzone = 20000` | `MARGIN_FPS[] = {2,2,3,4}`, `deadzone = 30000` |
| IO-Wait 检测 | `wakeups_100ms > 80 && util < 300` | `wakeups_100ms > 120 && util < 250` |
| Touch Boost | `touch_rate > 20`, `boost = rate*1500` | `touch_rate > 35`, `boost = rate*1000` |
| `apply_idle_freq` | `pow(0.8f, step)`, `interval = 60s` | `pow(0.7f, step)`, `interval = 30s` |

### 2. policy_engine.cpp

| 函数/位置 | 当前逻辑 | 优化建议 |
|:---|:---|:---|
| `decide()` EMA | `0.30f, 0.50f, 0.70f` | `0.20f, 0.40f, 0.60f` |
| 温控缩放 | `margin < 5 → 0.70f` | `margin < 15 → 0.85f`, `< 10 → 0.90f` |
| 游戏场景封顶 | 无硬上限 | `cfg.target_freq = min(cfg, 2200000u)` |

### 3. predictor.cpp

| 函数/位置 | 当前逻辑 | 优化建议 |
|:---|:---|:---|
| `alpha_10ms` | `0.7f` | 游戏场景 `0.45f` |
| IO-Wait Boost 上限 | 默认 100% | 降至 75% |

## 验收标准

| 指标 | 优化前 | 优化后目标 |
|:---|:---|:---|
| AVG FPS | 60.7 | 59.5 ~ 61.0 |
| 5% Low FPS | 59.1 | ≥ 58.0 |
| 平均功耗 | 3.37W | 2.6 ~ 2.9W |
| 表面温度 | ~38.2℃ | ≤ 37.0℃ |

## 架构建议

1. **核心绑定**：LOL 负载由 CPU 0-5 承担，屏蔽 CPU 6-7
2. **频率甜点**：8 Elite 性能核在 `1.5GHz` 附近能效比最高
3. **FAS 策略**：升频保守、降频果断