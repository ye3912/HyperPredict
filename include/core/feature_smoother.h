#pragma once
#include <cstdint>
#include <array>

namespace hp::core {

// =============================================================================
// FeatureSmoother — 统一的多时间尺度 EMA 平滑器
// 
// 消除 Predictor 和 PolicyEngine 之间的 EMA 计算重复。
// 使用模板参数 N_WINDOWS 支持不同窗口数量（Predictor=4, PolicyEngine=3）。
// =============================================================================
template <size_t N_WINDOWS>
class FeatureSmoother {
public:
    struct Config {
        std::array<float, N_WINDOWS> alphas;   // 每个窗口的 EMA alpha
        std::array<float, N_WINDOWS> defaults; // 初始值
    };

    explicit FeatureSmoother(const Config& cfg) noexcept : config_(cfg) {
        for (size_t i = 0; i < N_WINDOWS; ++i) {
            values_[i] = cfg.defaults[i];
        }
    }

    // 更新所有窗口的 EMA（返回更新前的值，用于趋势计算）
    std::array<float, N_WINDOWS> update(float new_value) noexcept {
        std::array<float, N_WINDOWS> old_vals = values_;
        for (size_t i = 0; i < N_WINDOWS; ++i) {
            values_[i] = values_[i] * (1.0f - config_.alphas[i]) + new_value * config_.alphas[i];
        }
        return old_vals;
    }

    // 获取指定窗口的值
    float get(size_t window) const noexcept { return values_[window]; }
    
    // 获取最快窗口（window 0）的值
    float fast() const noexcept { return values_[0]; }
    
    // 获取最慢窗口（最后一个）的值
    float slow() const noexcept { return values_[N_WINDOWS - 1]; }
    
    // 获取所有窗口值
    const std::array<float, N_WINDOWS>& values() const noexcept { return values_; }

    // 设置指定窗口的值（用于外部覆盖，如游戏模式）
    void set(size_t window, float val) noexcept { values_[window] = val; }

    // 批量更新 alpha（用于场景切换时动态调整）
    void set_alphas(const std::array<float, N_WINDOWS>& alphas) noexcept {
        config_.alphas = alphas;
    }

private:
    Config config_;
    std::array<float, N_WINDOWS> values_{};
};

// =============================================================================
// 趋势计算器 — 基于 EMA 变化率
// =============================================================================
class TrendCalculator {
public:
    // 更新趋势（输入: 当前 fast EMA 值, 上一次 fast EMA 值）
    void update(float current_fast, float old_fast, float current_fps, float old_fps) noexcept {
        float new_slope = (current_fast - old_fast) * SLOPE_SCALE;
        fps_trend_ = current_fps - old_fps;
        acceleration_ = (new_slope - last_slope_) * ACCEL_SCALE;
        last_slope_ = new_slope;
        slope_ = new_slope;
    }

    float slope() const noexcept { return slope_; }
    float fps_trend() const noexcept { return fps_trend_; }
    float acceleration() const noexcept { return acceleration_; }

private:
    float slope_{0.0f};
    float fps_trend_{0.0f};
    float acceleration_{0.0f};
    float last_slope_{0.0f};

    static constexpr float SLOPE_SCALE = 20.0f;   // 斜率缩放
    static constexpr float ACCEL_SCALE = 20.0f;    // 加速度缩放
};

} // namespace hp::core
