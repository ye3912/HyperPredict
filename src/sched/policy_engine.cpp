#include "sched/policy_engine.h"
#include "core/logger.h"
#include <cmath>
#include <algorithm>
#include <functional>

namespace hp::sched {

void PolicyEngine::init(const BaselinePolicy& baseline) noexcept {
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
    
    // 判断是否为日常场景
    bool is_daily = (scene && strcmp(scene, "Daily") == 0);
    
    // 1. 计算指数移动平均 (EMA) - 日常场景使用更长的窗口
    float util = static_cast<float>(f.cpu_util) / 1024.f;
    float fps = f.frame_interval_us > 0 ? (1000000.f / f.frame_interval_us) : target_fps;
    
    // 日常场景: 更平滑的 EMA 减少抖动
    float ema_util_alpha = is_daily ? 0.15f : 0.25f;
    float ema_fps_alpha = is_daily ? 0.08f : 0.15f;
    pred_state_.ewma_util = pred_state_.ewma_util * (1.0f - ema_util_alpha) + util * ema_util_alpha;
    pred_state_.ewma_fps = pred_state_.ewma_fps * (1.0f - ema_fps_alpha) + fps * ema_fps_alpha;
    
    // 2. 计算斜率 (50ms 窗口)
    static float last_util = 0.0f;
    pred_state_.util_slope_50ms = (util - last_util) * 20.0f;
    last_util = util;
    
    // 3. 计算趋势（加速度）
    float fps_error = target_fps - pred_state_.ewma_fps;
    pred_state_.trend = pred_state_.trend * 0.9f + fps_error * 0.1f;
    
    // 4. 预测未来利用率
    pred_state_.predicted_util_50ms = pred_state_.ewma_util + 
                                       (pred_state_.util_slope_50ms * 0.05f);
    pred_state_.predicted_util_50ms = std::clamp(pred_state_.predicted_util_50ms, 0.0f, 1.0f);
    
    // 5. 计算 boost 概率（日常场景更保守）
    float touch_factor = static_cast<float>(f.touch_rate_100ms) / 100.f;
    float wakeup_factor = static_cast<float>(f.wakeups_100ms) / 200.f;
    
    // 日常场景降低 boost 敏感度
    float touch_weight = is_daily ? 0.3f : 0.6f;
    float wakeup_weight = is_daily ? 0.2f : 0.4f;
    pred_state_.boost_prob = std::min(1.0f, touch_factor * touch_weight + wakeup_factor * wakeup_weight);
    
    // 6. 基础频率选择 - 日常场景更倾向小核
    // 日常: util > 0.6 才用大核; 游戏: util > 0.5
    float big_threshold = is_daily ? 0.6f : 0.5f;
    bool need_big = (pred_state_.ewma_util > big_threshold || 
                     f.is_gaming || 
                     f.run_queue_len > 3 ||
                     (pred_state_.boost_prob > 0.7f && !is_daily));
    
    const auto& base = need_big ? baseline_.big : baseline_.little;
    
    // 7. 频率计算模型（日常场景更保守）
    float load_factor = pred_state_.predicted_util_50ms;
    float fps_factor = std::clamp(pred_state_.ewma_fps / target_fps, 0.6f, 1.8f);
    float trend_factor = 1.0f + (pred_state_.trend / target_fps) * 0.3f;
    float slope_factor = 1.0f + std::clamp(pred_state_.util_slope_50ms, -0.5f, 0.8f) * 0.15f;
    
    // 日常场景降低趋势影响
    if (is_daily) {
        trend_factor = 1.0f + (pred_state_.trend / target_fps) * 0.15f;
    }
    
    // 综合计算目标频率
    float target_util = load_factor * fps_factor * trend_factor * slope_factor;
    target_util = std::clamp(target_util, 0.0f, 1.0f);
    
    cfg.target_freq = static_cast<uint32_t>(
        base.min_freq + (base.target_freq - base.min_freq) * target_util
    );
    
    // 8. 触摸加速（日常场景限制 boost）
    if (f.touch_rate_100ms > 50 && f.is_gaming) {
        uint32_t boost = std::min(250000u, static_cast<uint32_t>(touch_factor * 250000u));
        cfg.target_freq = std::min(cfg.target_freq + boost, base.target_freq);
    }
    
    // 9. 唤醒 boost（日常场景大幅降低）
    if (f.wakeups_100ms > 100) {
        uint32_t max_boost = is_daily ? 80000u : 150000u;
        uint32_t boost = std::min(max_boost, static_cast<uint32_t>(wakeup_factor * max_boost));
        cfg.target_freq = std::min(cfg.target_freq + boost, base.target_freq);
    }
    
    // 10. 游戏模式 boost
    if (f.is_gaming && pred_state_.boost_prob > 0.3f) {
        float game_boost = 1.0f + pred_state_.boost_prob * 0.3f;
        cfg.target_freq = std::min(
            static_cast<uint32_t>(cfg.target_freq * game_boost),
            base.target_freq
        );
    }
    
    // 11. 温控缩放（日常场景更敏感）
    float thermal_scale = 1.0f;
    if (is_daily) {
        // 日常场景更早降频
        if (f.thermal_margin < 8) {
            thermal_scale = 0.80f;
        } else if (f.thermal_margin < 12) {
            thermal_scale = 0.88f;
        } else if (f.thermal_margin < 18) {
            thermal_scale = 0.95f;
        }
    } else {
        if (f.thermal_margin < 5) {
            thermal_scale = 0.85f;
        } else if (f.thermal_margin < 10) {
            thermal_scale = 0.92f;
        } else if (f.thermal_margin < 15) {
            thermal_scale = 0.97f;
        }
    }
    cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * thermal_scale);
    
    // 12. 最小频率约束（日常场景更低）
    float min_ratio;
    if (f.is_gaming) {
        min_ratio = 0.8f;
    } else if (pred_state_.ewma_util > 0.6f) {
        min_ratio = is_daily ? 0.65f : 0.7f;
    } else if (pred_state_.ewma_util > 0.3f) {
        min_ratio = is_daily ? 0.5f : 0.55f;
    } else {
        min_ratio = is_daily ? 0.35f : 0.4f;  // 日常轻负载可降到更低
    }
    cfg.min_freq = std::max(
        static_cast<uint32_t>(cfg.target_freq * min_ratio),
        base.min_freq
    );
    
    // 13. uclamp 设置（日常场景更宽松）
    uint8_t uclamp_target = static_cast<uint8_t>(pred_state_.predicted_util_50ms * 100.f);
    if (is_daily) {
        cfg.uclamp_min = std::min(uclamp_target, static_cast<uint8_t>(70));
        cfg.uclamp_max = 95;  // 日常不跑满
    } else if (f.is_gaming) {
        cfg.uclamp_min = uclamp_target;
        cfg.uclamp_max = 100;
    } else {
        cfg.uclamp_min = std::min(uclamp_target, static_cast<uint8_t>(80));
        cfg.uclamp_max = 100;
    }
    
    // 14. 防抖检查（日常场景更激进防抖）
    uint32_t config_hash = std::hash<uint32_t>{}(cfg.target_freq ^ cfg.uclamp_max ^ cfg.min_freq);
    uint64_t now = 0;
    
    // 日常: 500ms 防抖; 游戏: 350ms
    uint64_t debounce_ns = is_daily ? 500000000ULL : 350000000ULL;
    
    bool skip_update = false;
    for (const auto& h : hist_) {
        if (h.last > 0 && (now - h.last) < debounce_ns && h.cfg_hash == config_hash) {
            skip_update = true;
            break;
        }
    }
    
    if (skip_update) {
        for (const auto& h : hist_) {
            if (h.last > 0) {
                cfg = h.cfg;
                break;
            }
        }
    } else {
        static int hist_idx = 0;
        hist_[hist_idx].last = now;
        hist_[hist_idx].cfg = cfg;
        hist_[hist_idx].cfg_hash = config_hash;
        hist_idx = (hist_idx + 1) % 3;
    }
    
    // 15. 日志输出
    if (scene && loop_count_ % 20 == 0) {
        LOGI("[%s] Freq=%u kHz | Util=%.1f%% | FPS=%.1f | Big=%d | ThermScale=%.2f",
             scene, cfg.target_freq, pred_state_.ewma_util * 100.f, 
             pred_state_.ewma_fps, need_big ? 1 : 0, thermal_scale);
    }
    
    return cfg;
}

void PolicyEngine::export_model(const char* path) noexcept {
    (void)path;
    LOGI("Model export: ewma_util=%.3f, trend=%.3f, boost_prob=%.3f",
         pred_state_.ewma_util, pred_state_.trend, pred_state_.boost_prob);
}

} // namespace hp::sched