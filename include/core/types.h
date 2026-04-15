#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace hp {
using Timestamp = uint64_t;
struct LoadFeature {
    uint32_t cpu_util{0}, run_queue_len{0}, wakeups_100ms{0};
    uint32_t frame_interval_us{0}, touch_rate_100ms{0};
    int32_t thermal_margin{20}, battery_level{100};
    bool is_gaming{false};
};
struct FreqConfig {
    uint32_t target_freq{0}, min_freq{0};
    uint8_t uclamp_min{0}, uclamp_max{100};
};
struct Baseline {
    FreqConfig big, mid, little;
};
}