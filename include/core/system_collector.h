#pragma once
#include <cstdint>
#include <string>
#include <array>
#include "core/types.h"

namespace hp {

class SystemCollector {
    struct CpuStat {
        uint64_t user, nice, system, idle, iowait, irq, softirq;
    };
    std::array<CpuStat, 8> prev_stat_{};
    std::array<uint32_t, 8> util_{};
    uint32_t last_rq_{0}, last_wake_{0};
    uint64_t last_frame_ts_{0};
    uint32_t touch_count_{0};
    uint64_t touch_ts_{0};
    
    bool read_cpu_stat();
    bool read_run_queue();
    bool read_thermal();
    bool read_battery();
    
public:
    SystemCollector() noexcept;
    LoadFeature collect() noexcept;
    uint32_t get_cpu_util(int cpu) const noexcept { return util_[cpu]; }
};

} // namespace hp