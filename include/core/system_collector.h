#pragma once
#include "core/types.h"
#include <string>
#include <fcntl.h>

namespace hp {

class SystemCollector {
    std::string proc_stat_path_;
    std::string thermal_base_path_;
    
    // 优化: fd 缓存
    int thermal_fds_[4] = {-1, -1, -1, -1};
    int battery_fd_ = -1;
    
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