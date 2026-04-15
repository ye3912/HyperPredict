#pragma once
#include <cstdint>

namespace hp {
using Timestamp = uint64_t;

struct LoadFeature {
    // 基础指标
    uint32_t cpu_util{0};           // 0-1024
    uint32_t run_queue_len{0};
    uint32_t wakeups_100ms{0};
    uint32_t frame_interval_us{0};
    uint32_t touch_rate_100ms{0};
    int32_t thermal_margin{20};
    int32_t battery_level{100};
    bool is_gaming{false};
    
    // 派生指标
    uint32_t current_fps{60};
    uint8_t load_intensity{0};      // 0-100
};

struct FreqConfig {
    uint32_t target_freq{0};
    uint32_t min_freq{0};
    uint8_t uclamp_min{0};
    uint8_t uclamp_max{100};
};

struct BaselinePolicy {
    FreqConfig big;
    FreqConfig mid;
    FreqConfig little;
};
}