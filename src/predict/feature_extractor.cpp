#include "predict/feature_extractor.h"
#include <algorithm>

namespace hp::predict {
LoadFeature FeatureExtractor::extract(uint32_t util, uint32_t rq, uint32_t wake,
                                      uint32_t frame, uint32_t touch, int8_t therm, uint8_t bat) noexcept {
    LoadFeature f{};
    f.cpu_util = std::min(1024u, util);
    f.run_queue_len = std::min(0xFFFFu, rq);
    f.wakeups_100ms = std::min(0xFFFFu, wake);
    f.frame_interval_us = frame;
    f.touch_rate_100ms = std::min(0xFFFFu, touch);
    f.thermal_margin = therm;
    f.battery_level = bat;

    ewma100_ = (ewma100_ * 3 + util) >> 2;
    ewma500_ = (ewma500_ * 7 + util) >> 3;
    slope50_ = std::clamp((int32_t)util - (int32_t)last_, -120, 120);

    f.util_ewma_100ms = ewma100_;
    f.util_ewma_500ms = ewma500_;
    f.util_slope_50ms = slope50_;
    f.predicted_util_50ms = std::min(1024u, ewma100_ + std::max(0, slope50_ >> 1));
    f.boost_prob = std::min(100u, (f.predicted_util_50ms * 100u) / 1024u + (slope50_ > 15 ? 15u : 0u));

    last_ = util;
    return f;
}

std::array<float, 10> FeatureExtractor::to_vec(const LoadFeature& f) const noexcept {
    return {
        f.cpu_util / 1024.f,
        f.run_queue_len / 16.f,
        f.wakeups_100ms / 100.f,
        (f.frame_interval_us > 0) ? (16666.f / f.frame_interval_us) : 0.f,
        f.touch_rate_100ms / 20.f,
        std::max(0.f, f.thermal_margin / 10.f),
        f.util_ewma_100ms / 1024.f,
        f.util_slope_50ms / 100.f,
        f.boost_prob / 100.f,
        f.predicted_util_50ms / 1024.f
    };
}
} // namespace hp::predict