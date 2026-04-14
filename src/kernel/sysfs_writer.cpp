#include "kernel/sysfs_writer.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>

namespace hp::kernel {

SysfsWriter::SysfsWriter() noexcept {
    for(int i = 0; i < 8; ++i) {
        std::string p = "/sys/devices/system/cpu/cpufreq/policy" + std::to_string(i);
        if(access((p + "/scaling_cur_freq").c_str(), F_OK) == 0) {
            cpus_[i].min_fd = open((p + "/scaling_min_freq").c_str(), O_WRONLY);
            cpus_[i].max_fd = open((p + "/scaling_max_freq").c_str(), O_WRONLY);
        }
    }
}

SysfsWriter::~SysfsWriter() noexcept {
    for(auto& c : cpus_) {
        if(c.min_fd >= 0) close(c.min_fd);
        if(c.max_fd >= 0) close(c.max_fd);
    }
}

bool SysfsWriter::set_batch(const std::vector<std::pair<int, FreqConfig>>& cfgs) noexcept {
    bool ok = true;
    for(auto& [cpu, cfg] : cfgs) {
        if(cpu < 0 || cpu >= 8 || cpus_[cpu].min_fd < 0) continue;
        
        int l1 = snprintf(cpus_[cpu].buf, 32, "%u", cfg.min_freq);
        int l2 = snprintf(cpus_[cpu].buf + 16, 16, "%u", cfg.target_freq);
        
        if(write(cpus_[cpu].min_fd, cpus_[cpu].buf, l1) != l1) ok = false;
        if(write(cpus_[cpu].max_fd, cpus_[cpu].buf + 16, l2) != l2) ok = false;
    }
    return ok;
}

} // namespace hp::kernel