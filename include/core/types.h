#pragma once
#include <cstdint>
#include <chrono>

namespace hp {
using Timestamp = uint64_t;
inline Timestamp now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

using FreqKHz = uint32_t;
using UtilFixed = uint16_t;
constexpr UtilFixed UTIL_SCALE = 1024;

struct alignas(64) LoadFeature {
    UtilFixed cpu_util, util_ewma_100ms, util_ewma_500ms;
    int16_t util_slope_50ms;
    uint16_t run_queue_len, wakeups_100ms, frame_interval_us, touch_rate_100ms;
    int8_t thermal_margin; uint8_t battery_level;
    UtilFixed predicted_util_50ms; uint8_t boost_prob;
    uint8_t _pad[14];
};

struct alignas(32) FreqConfig {
    FreqKHz min_freq, target_freq;
    uint16_t boost_duration_ms;
    uint8_t cpuset_mask, uclamp_min, uclamp_max, ramp_ms;
    uint32_t config_hash;
    constexpr FreqConfig() : min_freq(0), target_freq(0), boost_duration_ms(200),
        cpuset_mask(0xFF), uclamp_min(0), uclamp_max(100), ramp_ms(20), config_hash(0) {}
};

struct PowerModel { double a, b, c; };
struct BaselinePolicy {
    FreqConfig little, mid, big;
    uint8_t global_uclamp_max;
    PowerModel pwr;
};
} // namespace hp