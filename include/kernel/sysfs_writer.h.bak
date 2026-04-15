#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include "core/types.h"

namespace hp::kernel {

class SysfsWriter {
    bool uclamp_supported_{false};
    bool cgroups_supported_{false};
    std::string cgroup_path_;
    
    struct CpuData {
        int min_fd{-1};
        int max_fd{-1};
        char buf[32];
    };
    CpuData cpus_[8]{};
    
public:
    SysfsWriter();
    ~SysfsWriter();
    
    bool set_batch(const std::vector<std::pair<int, FreqConfig>>& batch) noexcept;
    bool set_frequency(int cpu, uint32_t freq) noexcept;
    bool set_uclamp(int cpu, uint8_t min, uint8_t max) noexcept;
    bool set_cpu_shares(int cpu, uint16_t shares) noexcept;
    bool uclamp_supported() const noexcept { return uclamp_supported_; }
    bool cgroups_supported() const noexcept { return cgroups_supported_; }
    
private:
    bool write_sysfs(const std::string& path, const std::string& value) noexcept;
    bool check_uclamp_support() noexcept;
    bool check_cgroups_support() noexcept;
    uint16_t uclamp_to_shares(uint8_t uclamp) noexcept;
};

} // namespace hp::kernel