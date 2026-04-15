#include "sched/policy_engine.h"
#include "core/logger.h"
#include <cmath>
#include <algorithm>
#include <functional>

namespace hp::sched {

void PolicyEngine::init(const BaselinePolicy& baseline) {
    baseline_ = baseline;
    
    // 初始化预测器状态
    pred_state_.last_update = 0;
    pred_state_.ewma_util = 0;
    pred_state_.ewma_fps = 0;
    pred_state_.trend = 0.0f;
    pred_state_.util_slope_50ms = 0.0f;
    pred_state_.boost_prob = 0.0f;
    pred_state_.predicted_util_50ms = 0.0f;
    
    // 初始化防抖历史
    for (auto& h : hist_) {
        h.last = 0;
        h.cfg = {};
        h.cfg_hash = 0;
    }
    
    loop_count_ = 0;
    
    LOGI("PolicyEngine initialized");
    LOGI("  Big: %u-%u kHz", baseline_.big.min_freq, baseline_.big.target_freq);
    LOGI("  Little: %u-%u kHz", baseline_.little.min_freq, baseline_.little.target_freq);
}

FreqConfig PolicyEngine::decide(const LoadFeature& f, float target_fps, const char* scene) noexcept {
    loop_count_++;
    FreqConfig cfg = {};
    
    // 1. 计算指数移动平均 (EMA) - 100ms 窗口
    float util = static_cast<float>(f.cpu_util) / 1024.f;
    float fps = f.frame_interval_us > 0 ? (1000000.f / f.frame_interval_us) : target_fps;
    
    pred_state_.ewma_util = pred_state_.ewma_util * 0.75f + util * 0.25f;
    pred_state_.ewma_fps = pred_state_.ewma_fps * 0.85f + fps * 0.15f;
    
    // 2. 计算斜率 (50ms 窗口)
    static float last_util = 0.0f;
    pred_state_.util_slope_50ms = (util - last_util) * 20.0f; // 斜率/秒
    last_util = util;
        // 3. 计算趋势（加速度）
    float fps_error = target_fps - pred_state_.ewma_fps;
    pred_state_.trend = pred_state_.trend * 0.9f + fps_error * 0.1f;
    
    // 4. 预测未来 50ms 利用率
    pred_state_.predicted_util_50ms = pred_state_.ewma_util + 
                                       (pred_state_.util_slope_50ms * 0.05f);
    pred_state_.predicted_util_50ms = std::clamp(pred_state_.predicted_util_50ms, 0.0f, 1.0f);
    
    // 5. 计算 boost 概率（触摸/唤醒加速）
    float touch_factor = static_cast<float>(f.touch_rate_100ms) / 100.f;
    float wakeup_factor = static_cast<float>(f.wakeups_100ms) / 200.f;
    pred_state_.boost_prob = std::min(1.0f, touch_factor * 0.6f + wakeup_factor * 0.4f);
    
    // 6. 基础频率选择
    bool need_big = (pred_state_.ewma_util > 0.5f || 
                     f.is_gaming || 
                     f.run_queue_len > 2 ||
                     pred_state_.boost_prob > 0.5f);
    
    const auto& base = need_big ? baseline_.big : baseline_.little;
    
    // 7. 频率计算模型（多因素综合）
    float load_factor = pred_state_.predicted_util_50ms;
    float fps_factor = std::clamp(pred_state_.ewma_fps / target_fps, 0.5f, 2.0f);
    float trend_factor = 1.0f + (pred_state_.trend / target_fps) * 0.5f;
    float slope_factor = 1.0f + std::clamp(pred_state_.util_slope_50ms, -1.0f, 1.0f) * 0.2f;
    
    // 综合计算目标频率
    float target_util = load_factor * fps_factor * trend_factor * slope_factor;
    target_util = std::clamp(target_util, 0.0f, 1.0f);
    
    cfg.target_freq = static_cast<uint32_t>(
        base.min_freq + (base.target_freq - base.min_freq) * target_util
    );
    
    // 8. 触摸加速（游戏场景）
    if (f.touch_rate_100ms > 50 && f.is_gaming) {
        uint32_t boost = std::min(300000u, static_cast<uint32_t>(touch_factor * 300000u));
        cfg.target_freq = std::min(cfg.target_freq + boost, base.target_freq);
    }
    
    // 9. 唤醒 boost
    if (f.wakeups_100ms > 100) {
        uint32_t boost = std::min(150000u, static_cast<uint32_t>(wakeup_factor * 150000u));
        cfg.target_freq = std::min(cfg.target_freq + boost, base.target_freq);
    }
    
    // 10. 游戏模式 boost
    if (f.is_gaming && pred_state_.boost_prob > 0.3f) {        float game_boost = 1.0f + pred_state_.boost_prob * 0.3f;
        cfg.target_freq = std::min(
            static_cast<uint32_t>(cfg.target_freq * game_boost),
            base.target_freq
        );
    }
    
    // 11. 温控缩放（乘性）
    float thermal_scale = 1.0f;
    if (f.thermal_margin < 5) {
        thermal_scale = 0.85f;
    } else if (f.thermal_margin < 10) {
        thermal_scale = 0.92f;
    } else if (f.thermal_margin < 15) {
        thermal_scale = 0.97f;
    }
    cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * thermal_scale);
    
    // 12. 最小频率约束
    float min_ratio = f.is_gaming ? 0.8f : (pred_state_.ewma_util > 0.6f ? 0.7f : 0.6f);
    cfg.min_freq = std::max(
        static_cast<uint32_t>(cfg.target_freq * min_ratio),
        base.min_freq
    );
    
    // 13. uclamp 设置（基于预测利用率）
    uint8_t uclamp_target = static_cast<uint8_t>(pred_state_.predicted_util_50ms * 100.f);
    cfg.uclamp_min = f.is_gaming ? uclamp_target : std::min(uclamp_target, static_cast<uint8_t>(80));
    cfg.uclamp_max = 100;
    
    // 14. 防抖检查（避免频繁切换）
    uint32_t config_hash = std::hash<uint32_t>{}(cfg.target_freq ^ cfg.uclamp_max ^ cfg.min_freq);
    uint64_t now = 0; // 简化：实际应使用 std::chrono
    
    bool skip_update = false;
    for (const auto& h : hist_) {
        if (h.last > 0 && (now - h.last) < 35000000 && h.cfg_hash == config_hash) {
            skip_update = true;
            break;
        }
    }
    
    if (skip_update) {
        // 使用上一次的配置
        for (const auto& h : hist_) {
            if (h.last > 0) {
                cfg = h.cfg;
                break;
            }
        }    } else {
        // 记录新配置（循环缓冲区）
        static int hist_idx = 0;
        hist_[hist_idx].last = now;
        hist_[hist_idx].cfg = cfg;
        hist_[hist_idx].cfg_hash = config_hash;
        hist_idx = (hist_idx + 1) % 3;
    }
    
    // 15. 日志输出
    if (scene && loop_count_ % 20 == 0) {
        LOGI("[%s] Freq=%u kHz | Util=%.1f%% | FPS=%.1f | Trend=%.2f | Slope=%.2f | Boost=%.0f%% | Therm=%.2f",
             scene, cfg.target_freq, pred_state_.ewma_util * 100.f, 
             pred_state_.ewma_fps, pred_state_.trend, pred_state_.util_slope_50ms,
             pred_state_.boost_prob * 100.f, thermal_scale);
    }
    
    return cfg;
}

void PolicyEngine::export_model(const char* path) noexcept {
    // 导出模型参数（简化版）
    (void)path;
    LOGI("Model export: ewma_util=%.3f, trend=%.3f, boost_prob=%.3f",
         pred_state_.ewma_util, pred_state_.trend, pred_state_.boost_prob);
}

} // namespace hp::sched