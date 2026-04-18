#include "kernel/sysfs_writer.h"
#include "core/logger.h"
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>

namespace hp::kernel {

SysfsWriter::SysfsWriter() {
    detect();
    for (int i = 0; i < 8; ++i) open_cpu(i);
}

SysfsWriter::~SysfsWriter() {
    // FdGuard 会自动清理
}

SysfsWriter::SysfsWriter(SysfsWriter&&) noexcept = default;
SysfsWriter& SysfsWriter::operator=(SysfsWriter&&) noexcept = default;

void SysfsWriter::detect() noexcept {
    std::ifstream f("/dev/cpuctl/cpu0/uclamp.min");
    if (f.good()) {
        bk_ = Backend::UCLAMP;
        LOGI("Backend: UCLAMP");
        return;
    }
    if (access("/sys/fs/cgroup/cgroup.subtree_control", F_OK) == 0) {
        cg_root_ = "/sys/fs/cgroup";
        bk_ = Backend::CGROUPS;
        LOGI("Backend: CGROUPS_V2");
        return;
    }
    if (access("/sys/fs/cgroup/cpu", F_OK) == 0) {
        cg_root_ = "/sys/fs/cgroup/cpu";
        bk_ = Backend::CGROUPS;
        LOGI("Backend: CGROUPS_V1");
        return;
    }
    bk_ = Backend::FREQ;
    LOGW("Backend: FREQ_ONLY");
}

bool SysfsWriter::open_cpu(int c) noexcept {
    if (c < 0 || c >= 8) return false;
    auto& f = fds_[c];
    char p[128];    
    snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", c);
    f.min_freq.reset(::open(p, O_WRONLY));
    
    snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", c);
    f.max_freq.reset(::open(p, O_WRONLY));
    
    if (bk_ == Backend::UCLAMP) {
        snprintf(p, sizeof(p), "/dev/cpuctl/cpu%d/uclamp.min", c);
        f.uclamp_min.reset(::open(p, O_WRONLY));
        snprintf(p, sizeof(p), "/dev/cpuctl/cpu%d/uclamp.max", c);
        f.uclamp_max.reset(::open(p, O_WRONLY));
    }
    return f.min_freq && f.max_freq;
}

bool SysfsWriter::write_min(int fd, uint32_t val) noexcept {
    if (fd < 0) return false;
    int len = snprintf(tl_min_buf_, sizeof(tl_min_buf_), "%u\n", val);
    return write(fd, tl_min_buf_, len) == len;
}

bool SysfsWriter::write_max(int fd, uint32_t val) noexcept {
    if (fd < 0) return false;
    int len = snprintf(tl_max_buf_, sizeof(tl_max_buf_), "%u\n", val);
    return write(fd, tl_max_buf_, len) == len;
}

bool SysfsWriter::write_uclamp(int fd, uint8_t val) noexcept {
    if (fd < 0) return false;
    int len = snprintf(tl_min_buf_, sizeof(tl_min_buf_), "%u\n", val);
    return write(fd, tl_min_buf_, len) == len;
}

bool SysfsWriter::write_cgroup(int c, uint8_t pct) noexcept {
    if (cg_root_.empty()) return false;
    char p[256];
    if (access((cg_root_ + "/cgroup.subtree_control").c_str(), F_OK) == 0) {
        snprintf(p, sizeof(p), "%s/system/cpu%d/cpu.weight", cg_root_.c_str(), c);
        std::ofstream(p) << (1 + (pct * 9999 / 100));
    } else {
        snprintf(p, sizeof(p), "%s/cpu%d/cpu.shares", cg_root_.c_str(), c);
        std::ofstream(p) << (2 + (pct * 262142 / 100));
    }
    return true;
}

bool SysfsWriter::apply(std::span<const std::pair<int, FreqConfig>> b) noexcept {
    bool ok = false;
    for (const auto& [cpu, cfg] : b) {
        if (cpu < 0 || cpu >= 8) continue;
        auto& f = fds_[cpu];
        
        if (bk_ == Backend::UCLAMP) {
            if (f.uclamp_min) write_uclamp(f.uclamp_min.get(), cfg.uclamp_min);
            if (f.uclamp_max) write_uclamp(f.uclamp_max.get(), cfg.uclamp_max);
        } else if (bk_ == Backend::CGROUPS) {
            write_cgroup(cpu, cfg.uclamp_max);
        }
        
        ok |= write_min(f.min_freq.get(), cfg.min_freq);
        ok |= write_max(f.max_freq.get(), cfg.target_freq);
    }
    return ok;
}

size_t SysfsWriter::apply_batch(const std::vector<std::pair<int, FreqConfig>>& b) noexcept {
    size_t success = 0;
    for (const auto& [cpu, cfg] : b) {
        if (cpu < 0 || cpu >= 8) continue;
        auto& f = fds_[cpu];
        
        if (bk_ == Backend::UCLAMP) {
            if (f.uclamp_min) write_uclamp(f.uclamp_min.get(), cfg.uclamp_min);
            if (f.uclamp_max) write_uclamp(f.uclamp_max.get(), cfg.uclamp_max);
        } else if (bk_ == Backend::CGROUPS) {
            write_cgroup(cpu, cfg.uclamp_max);
        }
        
        if (write_min(f.min_freq.get(), cfg.min_freq) && 
            write_max(f.max_freq.get(), cfg.target_freq)) {
            success++;
        }
    }
    return success;
}

std::string_view SysfsWriter::detect_cg_root() noexcept {
    if (access("/sys/fs/cgroup/cgroup.subtree_control", F_OK) == 0) {
        return "/sys/fs/cgroup";
    }
    if (access("/sys/fs/cgroup/cpu", F_OK) == 0) {
        return "/sys/fs/cgroup/cpu";
    }
    return {};
}

} // namespace hp::kernel