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
    UtilFixed cpu_util;
    UtilFixed util_ewma_100ms;
    UtilFixed util_ewma_500ms;
    uint16_t run_queue_len;
    uint16_t wakeups_100ms;
    int16_t util_slope_50ms;
    uint16_t frame_interval_us;
    uint16_t touch_rate_100ms;
    int8_t thermal_margin;
    uint8_t battery_level;
    UtilFixed predicted_util_50ms;
    uint8_t boost_prob;
    bool is_gaming;          // ✅ 新增
    uint8_t _pad[5];         // 调整填充
    
    LoadFeature() : cpu_util(0), util_ewma_100ms(0), util_ewma_500ms(0),
                    run_queue_len(0), wakeups_100ms(0), util_slope_50ms(0),
                    frame_interval_us(16000), touch_rate_100ms(0),
                    thermal_margin(5), battery_level(85),
                    predicted_util_50ms(0), boost_prob(0), is_gaming(false) {}
};

struct alignas(32) FreqConfig {
    FreqKHz min_freq;
    FreqKHz target_freq;
    uint16_t boost_duration_ms;
    uint8_t cpuset_mask;
    uint8_t uclamp_min;
    uint8_t uclamp_max;
    uint8_t ramp_ms;
    uint32_t config_hash;
    
    constexpr FreqConfig() : min_freq(0), target_freq(0), boost_duration_ms(200),
        cpuset_mask(0xFF), uclamp_min(0), uclamp_max(100), ramp_ms(20), config_hash(0) {}
};

struct PowerModel { 
    double a, b, c; 
    PowerModel() : a(1.2e-9), b(0.004), c(0.75) {}
};

struct BaselinePolicy {
    FreqConfig little;
    FreqConfig mid;
    FreqConfig big;
    uint8_t global_uclamp_max;
    PowerModel pwr;
    
    BaselinePolicy() : global_uclamp_max(85) {}
};

} // namespace hp