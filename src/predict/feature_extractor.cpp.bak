#include "predict/feature_extractor.h"

namespace hp::predict {

LoadFeature FeatureExtractor::extract(
    uint32_t cpu_util,
    uint32_t run_queue_len,
    uint32_t wakeups_100ms,
    uint32_t frame_interval_us,
    uint32_t touch_rate_100ms,
    int32_t thermal_margin,
    int32_t battery_level) noexcept {
    
    LoadFeature f;
    f.cpu_util = cpu_util;
    f.run_queue_len = run_queue_len;
    f.wakeups_100ms = wakeups_100ms;
    f.frame_interval_us = frame_interval_us;
    f.touch_rate_100ms = touch_rate_100ms;
    f.thermal_margin = thermal_margin;
    f.battery_level = battery_level;
    f.is_gaming = false; // 由 EventLoop 场景检测器设置
    
    return f;
}

} // namespace hp::predict