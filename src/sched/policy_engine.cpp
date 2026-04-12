#include "sched/policy_engine.h"
#include <cstring>
#include <algorithm>

namespace hp::sched {
std::array<float, 10> PolicyEngine::to_vec(const LoadFeature& f) const noexcept {
    return {
        f.cpu_util / 1024.f, f.run_queue_len / 16.f, f.wakeups_100ms / 100.f,
        (f.frame_interval_us > 0) ? (16666.f / f.frame_interval_us) : 0.f,
        f.touch_rate_100ms / 20.f, std::max(0.f, f.thermal_margin / 10.f),
        f.util_ewma_100ms / 1024.f, f.util_slope_50ms / 100.f,
        f.boost_prob / 100.f, f.predicted_util_50ms / 1024.f
    };
}

FreqConfig PolicyEngine::decide(const LoadFeature& f, float actual_fps, const char* pkg) noexcept {
    auto v = to_vec(f);
    float prob = pred_.predict(v);
    float act_util = std::min(1024.f, actual_fps / 60.f * 1024.f);
    
    fb_.record(f.predicted_util_50ms, act_util);
    fb_.try_recover();

    if(fb_.should_fallback()) {
        fb_.set_optimal(hist_.cfg);
        return fb_.optimal();
    }

    cache::Key k{}; std::strncpy(k.pkg, pkg, 63); std::strncpy(k.scene, "default", 31);
    if(auto c = cache_.get(k)) { pred_.update(v, actual_fps >= 56.f); return *c; }

    // 基线 + 动态偏移
    FreqConfig cfg = base_.big;
    FreqKHz shift = (prob > 0.75f) ? 400000 : (prob > 0.5f ? 200000 : 0);
    cfg.target_freq = std::min(3000000u, cfg.target_freq + shift);
    cfg.min_freq = cfg.target_freq * 0.82f;
    // ✅ 正确：统一类型
    uint8_t clamp_val = static_cast<uint8_t>(70 + prob * 30);
    cfg.uclamp_max = clamp_val < 100 ? clamp_val : 100;
    cfg.config_hash = std::hash<uint32_t>{}(cfg.target_freq ^ cfg.uclamp_max);

    Timestamp now = now_ns();
    if(now - hist_.last < 35000000 && cfg.config_hash == hist_.cfg.config_hash) {
        hist_.same++;
        if(hist_.same < 2) return hist_.cfg;
    } else {
        hist_.same = 0;
    }
    hist_.last = now; hist_.cfg = cfg;
    pred_.update(v, actual_fps >= 56.f);
    cache_.put(k, cfg);
    return cfg;
}
} // namespace hp::sched
