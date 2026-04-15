#include "kernel/sysfs_writer.h"
#include <fstream>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

namespace hp::kernel {

SysfsWriter::SysfsWriter() {
    uclamp_supported_ = check_uclamp_support();
    cgroups_supported_ = check_cgroups_support();
}

bool SysfsWriter::check_uclamp_support() noexcept {
    std::ifstream f("/proc/sched_debug");
    if(f) {
        std::string line;
        while(std::getline(f, line)) {
            if(line.find("uclamp") != std::string::npos) {
                return true;
            }
        }
    }
    
    char path[128];
    for(int cpu = 0; cpu < 8; ++cpu) {
        snprintf(path, sizeof(path), "/dev/cpuctl/cpu%d/uclamp.min", cpu);
        if(access(path, W_OK) == 0) {
            return true;
        }
    }
    
    return false;
}

bool SysfsWriter::check_cgroups_support() noexcept {
    if(access("/sys/fs/cgroup/cpu", F_OK) == 0) {
        cgroup_path_ = "/sys/fs/cgroup/cpu";
        return true;
    }
    
    if(access("/sys/fs/cgroup", F_OK) == 0) {
        cgroup_path_ = "/sys/fs/cgroup";
        return true;
    }
    
    if(access("/dev/cpuctl", F_OK) == 0) {
        cgroup_path_ = "/dev/cpuctl";
        return true;    }
    
    return false;
}

uint16_t SysfsWriter::uclamp_to_shares(uint8_t uclamp) noexcept {
    if(uclamp <= 10) return 2;
    if(uclamp >= 100) return 262144;
    return static_cast<uint16_t>((uclamp * 1024) / 100);
}

bool SysfsWriter::set_batch(const std::vector<std::pair<int, FreqConfig>>& batch) noexcept {
    bool success = true;
    
    for(const auto& [cpu, cfg] : batch) {
        if(!set_frequency(cpu, cfg.target_freq)) {
            success = false;
        }
        
        if(uclamp_supported_) {
            set_uclamp(cpu, cfg.uclamp_min, cfg.uclamp_max);
        } else if(cgroups_supported_) {
            uint16_t shares = uclamp_to_shares(cfg.uclamp_max);
            set_cpu_shares(cpu, shares);
        }
    }
    
    return success;
}

bool SysfsWriter::set_frequency(int cpu, FreqKHz freq) noexcept {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_setspeed", cpu);
    
    char value[16];
    snprintf(value, sizeof(value), "%u", freq);
    
    return write_sysfs(path, value);
}

bool SysfsWriter::set_uclamp(int cpu, uint8_t min, uint8_t max) noexcept {
    if(!uclamp_supported_) return false;
    
    char path_min[128], path_max[128];
    snprintf(path_min, sizeof(path_min), "/dev/cpuctl/cpu%d/uclamp.min", cpu);
    snprintf(path_max, sizeof(path_max), "/dev/cpuctl/cpu%d/uclamp.max", cpu);
    
    char value_min[16], value_max[16];
    snprintf(value_min, sizeof(value_min), "%u", min);
    snprintf(value_max, sizeof(value_max), "%u", max);    
    bool ok1 = write_sysfs(path_min, value_min);
    bool ok2 = write_sysfs(path_max, value_max);
    
    return ok1 && ok2;
}

bool SysfsWriter::set_cpu_shares(int cpu, uint16_t shares) noexcept {
    if(!cgroups_supported_) return false;
    
    char path[256];
    
    if(cgroup_path_.find("/sys/fs/cgroup/cpu") != std::string::npos) {
        snprintf(path, sizeof(path), "%s/cpu%d/cpu.shares", cgroup_path_.c_str(), cpu);
    } else if(cgroup_path_.find("/sys/fs/cgroup") != std::string::npos) {
        snprintf(path, sizeof(path), "%s/cpu%d/cpu.weight", cgroup_path_.c_str(), cpu);
        shares = static_cast<uint16_t>((shares * 10000) / 262144);
        if(shares < 1) shares = 1;
        if(shares > 10000) shares = 10000;
    } else {
        snprintf(path, sizeof(path), "%s/cpu%d/cpu.shares", cgroup_path_.c_str(), cpu);
    }
    
    char value[16];
    snprintf(value, sizeof(value), "%u", shares);
    
    return write_sysfs(path, value);
}

bool SysfsWriter::write_sysfs(const std::string& path, const std::string& value) noexcept {
    std::ofstream f(path);
    if(!f) return false;
    f << value;
    return !f.fail();
}

} // namespace hp::kernel