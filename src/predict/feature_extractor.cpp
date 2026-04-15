#include "predict/feature_extractor.h"
#include <algorithm>

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
    
    // 基础指标
    f.cpu_util = std::min(cpu_util, 1024u);
    f.run_queue_len = std::min(run_queue_len, 32u);
    f.wakeups_100ms = std::min(wakeups_100ms, 1000u);
    f.frame_interval_us = frame_interval_us;
    f.touch_rate_100ms = std::min(touch_rate_100ms, 200u);
    f.thermal_margin = thermal_margin;
    f.battery_level = std::clamp(battery_level, 0, 100);
    f.is_gaming = false;
    
    // 计算派生指标
    if (frame_interval_us > 0) {
        f.current_fps = static_cast<uint32_t>(1000000u / frame_interval_us);
    } else {
        f.current_fps = 60;
    }
    
    // 计算负载强度
    f.load_intensity = static_cast<uint8_t>(
        (static_cast<uint32_t>(cpu_util) * 100 / 1024 + 
         run_queue_len * 10 + 
         wakeups_100ms / 10) / 3
    );
    f.load_intensity = std::min(f.load_intensity, static_cast<uint8_t>(100));
    
    return f;
}

} // namespace hp::predict