#pragma once
#include <cstdint>

// =============================================================================
// 调度系统常量 — 消除魔法数字
// 
// 所有阈值集中定义，便于调优和跨组件一致性验证。
// =============================================================================

namespace hp::sched::constants {

// ========== 热阈值 (thermal_margin, 0-60 范围) ==========
// 统一三套热阈值方案 (event_loop / policy_engine / migration_engine)
namespace thermal {
    static constexpr int32_t EMERGENCY = 5;      // 紧急降频
    static constexpr int32_t CRITICAL = 8;       // 严重过热
    static constexpr int32_t HIGH = 10;          // 高温
    static constexpr int32_t MODERATE = 12;      // 中等过热
    static constexpr int32_t WARM = 20;          // 温热
    static constexpr int32_t ELEVATED = 30;      // 略高
    static constexpr int32_t HOT = 40;           // 热
} // namespace thermal

// ========== 负载阈值 (0-1024 范围) ==========
namespace load {
    static constexpr uint32_t IDLE = 51;         // ~5% 空闲
    static constexpr uint32_t VERY_LIGHT = 128;  // ~12.5% 极轻
    static constexpr uint32_t LIGHT = 256;       // ~25% 轻负载
    static constexpr uint32_t MEDIUM = 512;      // ~50% 中等
    static constexpr uint32_t HEAVY = 768;       // ~75% 重负载
    static constexpr uint32_t OVERUTIL = 870;    // ~85% 过载
    static constexpr uint32_t MAX = 1024;        // 100%
} // namespace load

// ========== FPS 阈值 ==========
namespace fps {
    static constexpr float DEFAULT = 60.0f;
    static constexpr float MAX = 144.0f;
    static constexpr float HIGH = 120.0f;
    static constexpr float MEDIUM = 90.0f;
    static constexpr float LOW = 30.0f;
} // namespace fps

// ========== 迁移阈值 ==========
namespace migration {
    static constexpr uint32_t LIGHT_LOAD = 20;    // 轻负载迁移抑制
    static constexpr uint32_t VERY_IDLE = 5;      // 非常空闲
    static constexpr uint32_t COOLING_PERIOD = 4; // 默认冷却期
    static constexpr uint32_t GAME_COOLING = 2;   // 游戏冷却期
    static constexpr uint32_t THERMAL_COOLING = 6; // 热迁移冷却期
} // namespace migration

// ========== EMA Alpha 值 ==========
namespace ema {
    // Predictor 多尺度窗口
    static constexpr float FAST = 0.7f;      // 10ms 窗口
    static constexpr float MEDIUM = 0.3f;    // 50ms 窗口
    static constexpr float SLOW = 0.1f;      // 200ms 窗口
    static constexpr float VERY_SLOW = 0.05f; // 500ms 窗口
    
    // 游戏模式
    static constexpr float GAME_FAST = 0.30f;
    static constexpr float GAME_MEDIUM = 0.50f;
    static constexpr float GAME_SLOW = 0.70f;
} // namespace ema

// ========== 功耗预算 ==========
namespace power {
    static constexpr float PRIME_REDUCTION = 0.8f;  // PRIME/BIG 核降频比例
    static constexpr float MID_REDUCTION = 0.9f;    // MID 核降频比例
    static constexpr float LITTLE_REDUCTION = 1.0f; // LITTLE 核不受影响
} // namespace power

// ========== 置信度门控 ==========
namespace confidence {
    static constexpr float CONSERVATIVE_MIN = 1.0f;
    static constexpr float CONSERVATIVE_MAX = 2.0f;
    static constexpr float CALIBRATION_MIN_SAMPLES = 10;
    static constexpr float CALIBRATION_MAX_SCALE = 10.0f;
    static constexpr float CALIBRATION_MIN_SCALE = 0.1f;
    static constexpr float CALIBRATION_MAX_BIAS = 50.0f;
} // namespace confidence

// ========== 频率滞回 ==========
namespace hysteresis {
    static constexpr float UP_THRESHOLD = 0.92f;    // 升频阈值
    static constexpr float DOWN_THRESHOLD = 1.05f;  // 降频阈值
    static constexpr int DOWN_COUNT = 5;            // 降频确认次数
} // namespace hysteresis

} // namespace hp::sched::constants
