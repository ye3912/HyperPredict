#pragma once
#include "core/types.h"
#include <string>

namespace hp {

class SystemCollector {
    std::string proc_stat_path_;
    std::string thermal_base_path_;
    
public:
    SystemCollector();
    LoadFeature collect() noexcept;
    bool is_gaming_scene() noexcept;
    
private:
    uint32_t read_cpu_util() noexcept;
    uint32_t read_run_queue() noexcept;
    uint32_t read_wakeups() noexcept;
    int8_t read_thermal_margin() noexcept;
    uint8_t read_battery_level() noexcept;
    uint32_t read_touch_rate() noexcept;
    uint32_t read_frame_interval() noexcept;
};

} // namespace hp