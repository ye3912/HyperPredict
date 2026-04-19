#pragma once
#include "core/types.h"

namespace hp::predict {

class FeatureExtractor {
public:
    LoadFeature extract(
        uint32_t cpu_util,
        uint32_t run_queue_len,
        uint32_t wakeups_100ms,
        uint32_t frame_interval_us,
        uint32_t touch_rate_100ms,
        int32_t thermal_margin,
        int32_t battery_level) noexcept;
};

} // namespace hp::predict