#include "kernel/sysfs_writer.h"
#include <fstream>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>

namespace hp::kernel {

SysfsWriter::SysfsWriter() {
    uclamp_supported_ = check_uclamp_support();
    cgroups_supported_ = check_cgroups_support();
}

SysfsWriter::~SysfsWriter() {
    for(auto& c : cpus_) {
        if(c.min_fd >= 0) close(c.min_fd);
        if(c.max_fd >= 0) close(c.max_fd);
    }
}

bool SysfsWriter::check_uclamp_support() noexcept {
    std::ifstream f("/proc/sched_debug");
    if(f) {
        std::string line;
        while(std::getline(f, line)) {
            if(line.find("uclamp") != std::string::npos) return true;
        }
    }
    char path[128];
    for(int i = 0; i < 8; ++i) {
        snprintf(path, sizeof(path), "/dev/cpuctl/cpu%d/uclamp.min", i);
        if(access(path, W_OK) == 0) return true;
    }
    return false;
}

bool SysfsWriter::check_cgroups_support() noexcept {
    if(access("/sys/fs/cgroup/cpu", F_OK) == 0) { cgroup_path_ = "/sys/fs/cgroup/cpu"; return true; }
    if(access("/sys/fs/cgroup", F_OK) == 0) { cgroup_path_ = "/sys/fs/cgroup"; return true; }
    if(access("/dev/cpuctl", F_OK) == 0) { cgroup_path_ = "/dev/cpuctl"; return true; }
    return false;
}

uint16_t SysfsWriter::uclamp_to_shares(uint8_t uclamp) noexcept {
    if(uclamp <= 10) return 2;
    if(uclamp >= 100) return 262144;
    return static_cast<uint16_t>((uclamp * 1024) / 100);}

bool SysfsWriter::set_batch(const std::vector<std::pair<int, FreqConfig>>& batch) noexcept {
    bool success = true;
    for(const auto& [cpu, cfg] : batch) {
        if(!set_frequency(cpu, cfg.target_freq)) success = false;
        if(uclamp_supported_) set_uclamp(cpu, cfg.uclamp_min, cfg.uclamp_max);
        else if(cgroups_supported_) set_cpu_shares(cpu, uclamp_to_shares(cfg.uclamp_max));
    }
    return success;
}

bool SysfsWriter::set_frequency(int cpu, uint32_t freq) noexcept {
    if(cpu < 0 || cpu >= 8) return false;
    if(cpus_[cpu].min_fd < 0) {
        char p[64]; snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
        cpus_[cpu].min_fd = open(p, O_WRONLY);
    }
    if(cpus_[cpu].max_fd < 0) {
        char p[64]; snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        cpus_[cpu].max_fd = open(p, O_WRONLY);
    }
    if(cpus_[cpu].min_fd < 0 || cpus_[cpu].max_fd < 0) return false;
    
    int len = snprintf(cpus_[cpu].buf, sizeof(cpus_[cpu].buf), "%u", freq);
    bool ok = true;
    if(write(cpus_[cpu].min_fd, cpus_[cpu].buf, len) != len) ok = false;
    if(write(cpus_[cpu].max_fd, cpus_[cpu].buf, len) != len) ok = false;
    return ok;
}

bool SysfsWriter::set_uclamp(int cpu, uint8_t min, uint8_t max) noexcept {
    if(!uclamp_supported_ || cpu < 0 || cpu >= 8) return false;
    char p1[128], p2[128];
    snprintf(p1, sizeof(p1), "/dev/cpuctl/cpu%d/uclamp.min", cpu);
    snprintf(p2, sizeof(p2), "/dev/cpuctl/cpu%d/uclamp.max", cpu);
    char v1[16], v2[16];
    snprintf(v1, sizeof(v1), "%u", min);
    snprintf(v2, sizeof(v2), "%u", max);
    return write_sysfs(p1, v1) && write_sysfs(p2, v2);
}

bool SysfsWriter::set_cpu_shares(int cpu, uint16_t shares) noexcept {
    if(!cgroups_supported_ || cpu < 0 || cpu >= 8) return false;
    char path[256];
    if(cgroup_path_.find("/sys/fs/cgroup/cpu") != std::string::npos)
        snprintf(path, sizeof(path), "%s/cpu%d/cpu.shares", cgroup_path_.c_str(), cpu);
    else if(cgroup_path_.find("/sys/fs/cgroup") != std::string::npos) {
        snprintf(path, sizeof(path), "%s/cpu%d/cpu.weight", cgroup_path_.c_str(), cpu);
        shares = static_cast<uint16_t>((shares * 10000) / 262144);        if(shares < 1) shares = 1; if(shares > 10000) shares = 10000;
    } else
        snprintf(path, sizeof(path), "%s/cpu%d/cpu.shares", cgroup_path_.c_str(), cpu);
    
    char value[16]; snprintf(value, sizeof(value), "%u", shares);
    return write_sysfs(path, value);
}

bool SysfsWriter::write_sysfs(const std::string& path, const std::string& value) noexcept {
    std::ofstream f(path);
    if(!f) return false;
    f << value;
    return !f.fail();
}

} // namespace hp::kernel