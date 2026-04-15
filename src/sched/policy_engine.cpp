// src/sched/policy_engine.cpp
// HyperPredict v4.0 - 智能调度策略引擎
// FAS+EMA+防抖完整实现 | 绝对调频 + 预测模型 + 场景感知

#include "sched/policy_engine.h"
#include "core/types.h"
#include "device/cpu_freq_manager.h"
#include <algorithm>
#include <cmath>
#include <array>

namespace hp::sched {

// ============================================================================
// 常量定义
// ============================================================================
constexpr uint64_t kDebounceWindowMs = 35;          // 防抖冷却窗
constexpr uint32_t kFasTargetUs = 2000;             // FAS 目标响应时间
constexpr float kFasHysteresis = 0.15f;             // 15% 迟滞带
constexpr float kEmaAlpha = 0.25f;                  // EMA 平滑系数
constexpr float kSlopeWeight = 0.3f;                // 斜率权重
constexpr float kTrendWeight = 0.2f;                // 趋势权重
constexpr float kTouchBoostFactor = 1.25f;          // 触摸提升系数
constexpr float kWakeupBoostFactor = 1.15f;         // 唤醒提升系数
constexpr int32_t kMinThermalMargin = 5;            // 最小温度余量(°C)

// ============================================================================
// 工具函数
// ============================================================================
namespace {

// ✅ 时间工具：纳秒级时间戳
inline uint64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// ✅ FAS 计算：绝对调频核心算法
inline uint32_t compute_fas_freq(uint32_t base_freq, float util, 
                                  float predicted_util, float therm_factor) noexcept {
    // 基础需求：按预测利用率缩放
    float target_util = std::max(util, predicted_util);
    uint32_t target_freq = static_cast<uint32_t>(base_freq * target_util / 1024.f);
    
    // 温度调节：余量不足时降频
    if (therm_factor < 1.0f) {
        target_freq = static_cast<uint32_t>(target_freq * therm_factor);
    }
        // 迟滞带：避免频繁振荡
    static uint32_t last_freq = 0;
    float hysteresis = base_freq * kFasHysteresis;
    
    if (target_freq > last_freq + hysteresis) {
        last_freq = target_freq;
    } else if (target_freq < last_freq - hysteresis) {
        last_freq = target_freq;
    }
    // else: 保持在 last_freq，触发迟滞
    
    // 指数衰减回归：平滑过渡
    last_freq = static_cast<uint32_t>(
        last_freq * 0.9f + target_freq * 0.1f
    );
    
    return std::clamp(last_freq, 300u, base_freq);
}

// ✅ 预测模型：EMA + 斜率 + 趋势 + Boost
inline float predict_util(const core::LoadFeature& f, 
                          float ema_util, float slope, float trend) noexcept {
    // 基础预测：加权组合
    float pred = ema_util * (1.0f - kSlopeWeight - kTrendWeight)
               + slope * kSlopeWeight
               + trend * kTrendWeight;
    
    // 触摸提升：高触摸率时额外提升
    if (f.touch_rate_100ms > 10) {
        pred *= kTouchBoostFactor;
    }
    
    // 唤醒提升：高唤醒率时适度提升
    if (f.wakeups_100ms > 20) {
        pred *= kWakeupBoostFactor;
    }
    
    // 游戏模式：固定高优先级
    if (f.is_gaming) {
        pred = std::max(pred, 768.f);  // 至少 75% 利用率
    }
    
    return std::clamp(pred, 0.f, 1024.f);
}

// ✅ 防抖校验：配置哈希 + 时间窗 + 循环缓冲
inline bool should_debounce(uint64_t& last_ts, uint32_t& hash_ring, 
                            uint32_t new_hash, uint64_t now) noexcept {
    constexpr size_t kRingSize = 8;
    constexpr uint32_t kHashMask = 0xFF;    
    // 时间窗检查
    if (now - last_ts < kDebounceWindowMs * 1'000'000ULL) {
        // 哈希环检查：避免相同配置重复执行
        uint32_t slot = new_hash & (kRingSize - 1);
        if (hash_ring & (1u << slot)) {
            return true;  // 需要防抖
        }
        hash_ring |= (1u << slot);
    } else {
        // 超时重置
        last_ts = now;
        hash_ring = 0;
    }
    return false;
}

} // anonymous namespace

// ============================================================================
// PolicyEngine 成员函数实现
// ============================================================================

PolicyEngine::PolicyEngine() noexcept 
    : baseline_{}
    , ema_util_{512.f}
    , slope_{0.f}
    , trend_{0.f}
    , last_freq_{}, last_util_{}, last_ts_{0}
    , debounce_hash_{0}, debounce_ts_{0}
    , config_hash_{0}
    , fas_enabled_{true}
    , migration_enabled_{true}
    , current_mode_{core::PolicyMode::BALANCED} {
    last_freq_.fill(800000);  // 默认 800MHz 起始
    last_util_.fill(512);
}

// ✅ 修复: 添加 noexcept 以匹配头文件声明（请核对 policy_engine.h）
void PolicyEngine::init(const core::BaselinePolicy& baseline) noexcept {
    baseline_ = baseline;
    
    // 初始化频率表
    for (size_t i = 0; i < last_freq_.size(); ++i) {
        last_freq_[i] = baseline.min_freq;
    }
    
    // 重置状态
    ema_util_ = 512.f;
    slope_ = 0.f;    trend_ = 0.f;
    debounce_hash_ = 0;
    debounce_ts_ = 0;
    config_hash_ = 0;
}

// ✅ 核心调度入口：每个调度周期调用
core::FreqConfig PolicyEngine::compute(uint32_t cpu_id, 
                                        const core::LoadFeature& features,
                                        const device::FreqTable& freq_table) noexcept {
    const uint64_t now = now_ns();
    
    // 1️⃣ 防抖预检
    uint32_t feature_hash = std::hash<uint64_t>{}(
        static_cast<uint64_t>(features.cpu_util) ^
        (static_cast<uint64_t>(features.run_queue_len) << 16) ^
        (static_cast<uint64_t>(features.wakeups_100ms) << 32)
    );
    
    if (should_debounce(debounce_ts_, debounce_hash_, feature_hash, now)) {
        // 防抖命中：返回上次结果
        return { last_freq_[cpu_id], last_freq_[cpu_id], false };
    }
    
    // 2️⃣ EMA 更新：指数移动平均
    float current_util = static_cast<float>(features.cpu_util);
    ema_util_ = ema_util_ * (1.f - kEmaAlpha) + current_util * kEmaAlpha;
    
    // 3️⃣ 斜率计算：短期变化率
    float prev_util = static_cast<float>(last_util_[cpu_id]);
    slope_ = std::clamp((current_util - prev_util) * 2.f, -256.f, 256.f);
    
    // 4️⃣ 趋势计算：长期方向（简化版）
    trend_ = trend_ * 0.95f + slope_ * 0.05f;
    
    // 5️⃣ 预测利用率
    float therm_factor = (features.thermal_margin > kMinThermalMargin) 
        ? 1.0f 
        : static_cast<float>(features.thermal_margin) / kMinThermalMargin;
    
    float predicted = predict_util(features, ema_util_, slope_, trend_);
    
    // 6️⃣ FAS 绝对调频
    uint32_t base_freq = freq_table.max_freq;
    uint32_t target_freq = fas_enabled_ 
        ? compute_fas_freq(base_freq, current_util, predicted, therm_factor)
        : baseline_.default_freq;
    
    // 7️⃣ 硬件步进对齐（由调用方保证，此处做最终裁剪）
    target_freq = std::clamp(target_freq, freq_table.min_freq, freq_table.max_freq);    
    // 8️⃣ 更新状态
    last_freq_[cpu_id] = target_freq;
    last_util_[cpu_id] = features.cpu_util;
    
    // 9️⃣ 构建返回配置
    core::FreqConfig config{};
    config.min_freq = target_freq;  // FAS 模式下 min=max=目标值
    config.max_freq = target_freq;
    config.boost = (features.touch_rate_100ms > 15 || features.is_gaming);
    
    return config;
}

// ✅ 模式切换：场景化策略
void PolicyEngine::set_mode(core::PolicyMode mode) noexcept {
    current_mode_ = mode;
    
    switch (mode) {
        case core::PolicyMode::PERFORMANCE:
            baseline_.target_util = 900;  // 90% 目标利用率
            baseline_.aggressiveness = 1.2f;
            break;
        case core::PolicyMode::POWERSAVE:
            baseline_.target_util = 400;  // 40% 目标利用率
            baseline_.aggressiveness = 0.7f;
            break;
        case core::PolicyMode::GAME:
            baseline_.target_util = 850;
            baseline_.aggressiveness = 1.1f;
            break;
        case core::PolicyMode::BALANCED:
        default:
            baseline_.target_util = 650;
            baseline_.aggressiveness = 1.0f;
            break;
    }
}

// ✅ 温控回调：动态调整策略
void PolicyEngine::on_thermal_event(int32_t margin) noexcept {
    if (margin < kMinThermalMargin) {
        // 温度紧张：禁用 FAS，回退到保守策略
        fas_enabled_ = false;
        migration_enabled_ = true;  // 启用迁移散热
    } else if (margin > kMinThermalMargin + 10) {
        // 温度恢复：重新启用高级特性
        fas_enabled_ = true;
    }
    // else: 保持当前状态}

// ✅ 调试信息：用于 WebUI 日志
std::string PolicyEngine::get_debug_info(uint32_t cpu_id) const noexcept {
    char buf[256];
    snprintf(buf, sizeof(buf), 
        "CPU%u: F=%ukHz U=%.1f%% EMA=%.1f S=%.1f T=%.1f DB=%ums",
        cpu_id,
        last_freq_[cpu_id],
        last_util_[cpu_id] / 10.24f,
        ema_util_ / 10.24f,
        slope_,
        trend_,
        static_cast<uint32_t>((now_ns() - debounce_ts_) / 1'000'000ULL)
    );
    return std::string(buf);
}

// ✅ 状态序列化：用于持久化/热更新
std::array<uint8_t, 64> PolicyEngine::serialize_state() const noexcept {
    std::array<uint8_t, 64> buf{};
    size_t offset = 0;
    
    // 简单二进制序列化（生产环境建议用 protobuf）
    auto write_u32 = [&](uint32_t v) {
        if (offset + 4 <= buf.size()) {
            buf[offset++] = (v >> 24) & 0xFF;
            buf[offset++] = (v >> 16) & 0xFF;
            buf[offset++] = (v >> 8) & 0xFF;
            buf[offset++] = v & 0xFF;
        }
    };
    
    auto write_f32 = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        write_u32(bits);
    };
    
    write_u32(static_cast<uint32_t>(current_mode_));
    write_f32(ema_util_);
    write_f32(slope_);
    write_f32(trend_);
    write_u32(config_hash_);
    // ... 其他字段按需添加
    
    return buf;
}

// ✅ 状态反序列化bool PolicyEngine::deserialize_state(const std::array<uint8_t, 64>& buf) noexcept {
    if (buf[0] == 0) return false;  // 空状态检查
    
    size_t offset = 0;
    auto read_u32 = [&](uint32_t& v) -> bool {
        if (offset + 4 > buf.size()) return false;
        v = (static_cast<uint32_t>(buf[offset++]) << 24) |
            (static_cast<uint32_t>(buf[offset++]) << 16) |
            (static_cast<uint32_t>(buf[offset++]) << 8) |
            static_cast<uint32_t>(buf[offset++]);
        return true;
    };
    
    auto read_f32 = [&](float& v) -> bool {
        uint32_t bits;
        if (!read_u32(bits)) return false;
        std::memcpy(&v, &bits, sizeof(v));
        return true;
    };
    
    uint32_t mode_val;
    if (!read_u32(mode_val)) return false;
    current_mode_ = static_cast<core::PolicyMode>(mode_val);
    
    read_f32(ema_util_);
    read_f32(slope_);
    read_f32(trend_);
    read_u32(config_hash_);
    
    return true;
}

} // namespace hp::sched