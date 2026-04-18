#include "sched/policy_engine.h"
#include "core/logger.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>

namespace hp::sched {

// =============================================================================
// schedutil 核心公式常量 - 类比 CNN 论文的查表化简
// =============================================================================
static constexpr float MAP_UTIL_FREQ_SCALE = 1.25f;      // 临界点 0.8 的系数
[[maybe_unused]] static constexpr float UTIL_TIP_POINT = 0.80f;            // 频率映射临界点
[[maybe_unused]] static constexpr uint64_t RATE_LIMIT_MIN_US = 1000ULL;    // 1ms 最小调频间隔

// =============================================================================
// 频率映射预计算表 - 类比 CNN 论文的查表化简
// 预计算不同 util (0-1024) 映射到频率的索引
// =============================================================================
class FreqMapTable {
private:
    std::vector<uint32_t> table_;  // util (0-1024) → freq index
    uint32_t freq_count_{0};
    uint32_t max_freq_{0};
    std::vector<uint32_t> freq_steps_;
    
public:
    void init(uint32_t max_freq, const std::vector<uint32_t>& steps) noexcept {
        max_freq_ = max_freq;
        freq_steps_ = steps;
        freq_count_ = static_cast<uint32_t>(steps.size());
        
        // 预计算 1024 个条目的映射表 (索引 0-1023)
        table_.resize(1024);
        
        for (int util = 0; util < 1024; util++) {
            float util_norm = util / 1024.0f;
            
            // schedutil 公式: freq = C * max * util
            // 即: next_freq = MAP_UTIL_FREQ_SCALE * max_freq * util
            uint32_t target_freq = static_cast<uint32_t>(
                MAP_UTIL_FREQ_SCALE * max_freq_ * util_norm
            );
            
            // 映射到最近的可用频点 (使用二分查找优化)
            uint32_t freq_idx = 0;
            if (!freq_steps_.empty() && target_freq > 0) {
                // 二分查找第一个 > target_freq 的位置
                size_t left = 0;
                size_t right = freq_steps_.size();
                while (left < right) {
                    size_t mid = left + (right - left) / 2;
                    if (freq_steps_[mid] <= target_freq) {
                        left = mid + 1;
                    } else {
                        right = mid;
                    }
                }
                freq_idx = static_cast<uint32_t>(left);
                if (freq_idx >= freq_steps_.size()) {
                    freq_idx = static_cast<uint32_t>(freq_steps_.size() - 1);
                }
            }
            
            table_[util] = freq_idx;
        }
    }
    
    // O(1) 查表获取目标频率
    uint32_t get_freq(uint32_t cpu_util) const noexcept {
        if (table_.empty() || freq_steps_.empty()) {
            return 0;
        }
        
        uint32_t idx = std::min(cpu_util, 1023u);
        return freq_steps_[table_[idx]];
    }
    
    uint32_t get_freq_count() const noexcept { return freq_count_; }
    uint32_t get_max_freq() const noexcept { return max_freq_; }
};

// =============================================================================
// PolicyEngine 实现
// =============================================================================

struct PolicyEngine::Impl {
    // 频率映射表
    FreqMapTable big_freq_table_;
    FreqMapTable little_freq_table_;

    // Rate limiting 状态
    uint64_t last_freq_update_ns_{0};
    uint32_t rate_limit_us_{1000};  // 默认 1ms

    // 频率保持状态 (类比 Uperf 的延迟终止升频)
    uint64_t hold_freq_until_ns_{0};
    uint32_t held_freq_{0};

    // IO-Wait Boost 状态
    uint32_t io_wait_boost_{0};
    bool io_wait_pending_{false};

    // 渲染感知状态
    bool frame_rendering_{false};
    uint64_t last_frame_end_ns_{0};
    static constexpr uint64_t FRAME_HOLD_NS = 66000;  // 66ms 帧保持

    // 多时间尺度状态
    float ewma_util_short_{0.0f};   // 10ms
    float ewma_util_medium_{0.0f};  // 50ms
    float ewma_util_long_{0.0f};    // 200ms

    float ewma_fps_short_{60.0f};
    float ewma_fps_long_{60.0f};

    // 趋势状态
    float util_slope_{0.0f};
    float fps_trend_{0.0f};
    float acceleration_{0.0f};

    // 特征历史
    uint32_t util_history_[8]{0};
    uint8_t history_idx_{0};

    // ✅ 新增：最低频率 (空闲时可下探到此频率)
    uint32_t min_freq_khz_{300000};  // 默认 300MHz
};

PolicyEngine::PolicyEngine() noexcept : impl_(std::make_unique<Impl>()) {}

PolicyEngine::~PolicyEngine() noexcept = default;

void PolicyEngine::init(const BaselinePolicy& baseline) noexcept {
    baseline_ = baseline;

    // 初始化频率映射预计算表
    impl_->big_freq_table_.init(
        baseline_.big.target_freq,
        {baseline_.big.min_freq, baseline_.big.target_freq}
    );
    impl_->little_freq_table_.init(
        baseline_.little.target_freq,
        {baseline_.little.min_freq, baseline_.little.target_freq}
    );

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

    LOGI("PolicyEngine initialized with enhanced algorithm");
    LOGI("  Big: %u-%u kHz", baseline_.big.min_freq, baseline_.big.target_freq);
    LOGI("  Little: %u-%u kHz", baseline_.little.min_freq, baseline_.little.target_freq);
    LOGI("  Rate limit: %u us", impl_->rate_limit_us_);
}

void PolicyEngine::set_min_freq(uint32_t min_freq_khz) noexcept {
    impl_->min_freq_khz_ = min_freq_khz;
    LOGI("PolicyEngine min_freq set to %u kHz", min_freq_khz);
}

FreqConfig PolicyEngine::decide(const LoadFeature& f, float target_fps, const char* scene) noexcept {
    loop_count_++;
    FreqConfig cfg = {};
    
    // ========== 1. 时间戳和场景判断 ==========
    uint64_t now_ns = 0;  // 需要从外部传入
    (void)now_ns;
    
    bool is_daily = (scene && strcmp(scene, "Daily") == 0);
    bool is_gaming = f.is_gaming || (scene && strcmp(scene, "Game") == 0);
    
    // ========== 2. 多时间尺度 EMA ==========
    float util = static_cast<float>(f.cpu_util) / 1024.0f;
    float current_fps = f.frame_interval_us > 0 ? 
        (1000000.0f / f.frame_interval_us) : target_fps;
    
    // 更新历史
    impl_->util_history_[impl_->history_idx_ % 8] = f.cpu_util;
    impl_->history_idx_++;
    
    // 多时间尺度 EMA
    impl_->ewma_util_short_ = impl_->ewma_util_short_ * 0.7f + util * 0.3f;
    impl_->ewma_util_medium_ = impl_->ewma_util_medium_ * 0.5f + util * 0.5f;
    impl_->ewma_util_long_ = impl_->ewma_util_long_ * 0.3f + util * 0.7f;
    
    impl_->ewma_fps_short_ = impl_->ewma_fps_short_ * 0.7f + current_fps * 0.3f;
    impl_->ewma_fps_long_ = impl_->ewma_fps_long_ * 0.3f + current_fps * 0.7f;
    
    // ========== 3. 趋势计算 ==========
    float prev_util = impl_->ewma_util_short_;
    impl_->util_slope_ = (util - prev_util) * 20.0f;
    impl_->fps_trend_ = current_fps - impl_->ewma_fps_short_;
    
    // 二阶导数 (加速度)
    static float last_slope = 0.0f;
    impl_->acceleration_ = (impl_->util_slope_ - last_slope) * 20.0f;
    last_slope = impl_->util_slope_;
    
    // ========== 4. schedutil 频率映射 ==========
    // 使用 schedutil 公式: next_freq = C * max_freq * util
    // 其中 C = 1.25，临界点 util = 0.8
    
    // 基础频率选择 - 日常应用功耗优化
    float big_threshold = is_daily ? 0.75f : 0.55f;  // 日常应用时提高大核阈值，从 0.65f 提高到 0.75f
    bool need_big = (impl_->ewma_util_medium_ > big_threshold ||
                     is_gaming ||
                     f.run_queue_len > 3 ||
                     impl_->io_wait_pending_);
    
    // 使用预计算表获取基础频率
    uint32_t base_freq = need_big ? 
        impl_->big_freq_table_.get_freq(f.cpu_util) :
        impl_->little_freq_table_.get_freq(f.cpu_util);
    
    // 如果表太简单，使用线性计算
    if (base_freq == 0) {
        const auto& base = need_big ? baseline_.big : baseline_.little;
        base_freq = static_cast<uint32_t>(
            MAP_UTIL_FREQ_SCALE * base.target_freq * util
        );
        base_freq = std::clamp(base_freq, base.min_freq, base.target_freq);
    }
    
    cfg.target_freq = base_freq;
    
    // ========== 5. FPS 误差修正 ==========
    float fps_error = target_fps - impl_->ewma_fps_short_;
    float fps_correction = 1.0f;
    
    if (std::abs(fps_error) > 5.0f) {
        // FPS 误差 > 5，应用修正
        fps_correction = 1.0f + (fps_error / target_fps) * 0.5f;
        fps_correction = std::clamp(fps_correction, 0.85f, 1.20f);
    }
    
    cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * fps_correction);
    
    // ========== 6. IO-Wait Boost ==========
    if (impl_->io_wait_pending_ || f.wakeups_100ms > 80) {
        // IO 密集型任务，逐步 boost
        impl_->io_wait_boost_ = std::min(impl_->io_wait_boost_ + 64u, 256u);
        cfg.target_freq = std::min(
            cfg.target_freq + (impl_->io_wait_boost_ * 1000u),
            need_big ? baseline_.big.target_freq : baseline_.little.target_freq
        );
    } else if (impl_->io_wait_boost_ > 0) {
        // 衰减
        impl_->io_wait_boost_ = impl_->io_wait_boost_ * 7 / 8;
    }
    
    // ========== 7. 触摸加速 ==========
    if (f.touch_rate_100ms > 20) {
        // 触摸时立即 boost
        uint32_t touch_boost = std::min(f.touch_rate_100ms * 2000, 300000u);
        cfg.target_freq = std::min(cfg.target_freq + touch_boost,
                                   need_big ? baseline_.big.target_freq : baseline_.little.target_freq);
        
        // 触发帧保持
        impl_->hold_freq_until_ns_ = 0;  // 需要外部时间戳
        impl_->held_freq_ = cfg.target_freq;
    }
    
    // ========== 8. 帧渲染感知 ==========
    // 帧结束后保持高频 66ms (类比 Uperf)
    // if (impl_->last_frame_end_ns_ > 0 && now_ns - impl_->last_frame_end_ns_ < FRAME_HOLD_NS) {
    //     cfg.target_freq = std::max(cfg.target_freq, impl_->held_freq_);
    // }
    
    // ========== 9. 趋势修正 - 日常应用功耗优化 ==========
    // 上升趋势提前升频，下降趋势延迟降频
    if (impl_->acceleration_ > 0.1f) {
        // 加速上升，稍微多给一点频率
        cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * 1.05f);
    } else if (impl_->acceleration_ < -0.1f) {
        // 减速下降，稍微保守一点
        cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * (is_daily ? 0.99f : 0.98f));  // 日常应用时从 0.98f 提高到 0.99f
    }
    
    // ========== 10. 温控缩放 - 日常应用功耗优化 ==========
    float thermal_scale = 1.0f;
    if (f.thermal_margin < 5) {
        thermal_scale = is_daily ? 0.75f : 0.80f;  // 日常应用时从 0.80f 降低到 0.75f
    } else if (f.thermal_margin < 10) {
        thermal_scale = is_daily ? 0.85f : 0.90f;  // 日常应用时从 0.90f 降低到 0.85f
    } else if (f.thermal_margin < 15) {
        thermal_scale = is_daily ? 0.93f : 0.96f;  // 日常应用时从 0.96f 降低到 0.93f
    }
    cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * thermal_scale);
    
    // ========== 11. 边界约束 ==========
    const auto& base = need_big ? baseline_.big : baseline_.little;
    cfg.target_freq = std::clamp(cfg.target_freq, base.min_freq, base.target_freq);
    
    // ========== 12. Rate Limiting ==========
    // 确保不频繁调频 (已在主循环处理)
    
    // ========== 13. 最小频率约束 - 日常应用功耗优化 ==========
    // 空闲时可下探到 SoC 配置的最低频率以降低静止功耗
    float min_ratio;
    if (is_gaming) {
        min_ratio = 0.75f;
    } else if (impl_->ewma_util_medium_ > 0.5f) {
        min_ratio = is_daily ? 0.60f : 0.60f;  // 日常应用时从 0.55f 提高到 0.60f
    } else if (impl_->ewma_util_medium_ > 0.25f) {
        min_ratio = is_daily ? 0.45f : 0.45f;  // 日常应用时从 0.40f 提高到 0.45f
    } else {
        // 空闲状态：使用 SoC 配置的最低频率
        min_ratio = is_daily ? 0.20f : 0.20f;  // 日常应用时从 0.15f 提高到 0.20f
    }

    // 计算最小频率，但不能低于 SoC 配置的最低频率
    uint32_t calc_min_freq = static_cast<uint32_t>(cfg.target_freq * min_ratio);
    cfg.min_freq = std::max(calc_min_freq, impl_->min_freq_khz_);

    // 确保最小频率不超过目标频率
    cfg.min_freq = std::min(cfg.min_freq, cfg.target_freq);
    
    // ========== 14. UCLamp 设置 - 日常应用功耗优化 ==========
    uint8_t uclamp_target = static_cast<uint8_t>(impl_->ewma_util_medium_ * 100.0f);
    if (is_daily) {
        cfg.uclamp_min = std::min(uclamp_target, static_cast<uint8_t>(65));  // 日常应用时从 70 降低到 65
        cfg.uclamp_max = 90;  // 日常应用时从 95 降低到 90
    } else if (is_gaming) {
        cfg.uclamp_min = uclamp_target;
        cfg.uclamp_max = 100;
    } else {
        cfg.uclamp_min = std::min(uclamp_target, static_cast<uint8_t>(80));
        cfg.uclamp_max = 100;
    }
    
    // ========== 15. 防抖历史 ==========
    uint32_t config_hash = std::hash<uint32_t>{}(
        cfg.target_freq ^ (cfg.uclamp_max << 16) ^ cfg.min_freq
    );
    
    // 日志
    if (loop_count_ % 20 == 0) {
        LOGI("[%s] Freq=%u kHz | Util=%.1f%% | FPS=%.1f | Big=%d | IOBoost=%u | ThermScale=%.2f",
             scene ? scene : "Unknown",
             cfg.target_freq, 
             impl_->ewma_util_medium_ * 100.0f,
             impl_->ewma_fps_short_,
             need_big ? 1 : 0,
             impl_->io_wait_boost_,
             thermal_scale);
    }
    
    return cfg;
}

void PolicyEngine::export_model(const char* path) noexcept {
    (void)path;
    LOGI("PolicyEngine model: ewma_util=%.3f, trend=%.3f, io_boost=%u",
         impl_->ewma_util_medium_, impl_->util_slope_, impl_->io_wait_boost_);
}

// ========== 新增接口实现 ==========

void PolicyEngine::set_io_wait_boost(bool has_iowait) noexcept {
    impl_->io_wait_pending_ = has_iowait;
}

void PolicyEngine::on_frame_end() noexcept {
    impl_->last_frame_end_ns_ = 0;  // 需要实际时间戳
}

bool PolicyEngine::should_update_freq(uint64_t now_ns) const noexcept {
    if (impl_->last_freq_update_ns_ == 0) {
        return true;
    }
    
    int64_t delta_us = (now_ns - impl_->last_freq_update_ns_) / 1000;
    return delta_us >= static_cast<int64_t>(impl_->rate_limit_us_);
}

void PolicyEngine::update_freq_timestamp(uint64_t now_ns) noexcept {
    impl_->last_freq_update_ns_ = now_ns;
}

uint32_t PolicyEngine::get_io_wait_boost() const noexcept {
    return impl_->io_wait_boost_;
}

float PolicyEngine::get_util_trend() const noexcept {
    return impl_->util_slope_;
}

} // namespace hp::sched
