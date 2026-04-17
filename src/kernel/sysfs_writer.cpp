#include "kernel/sysfs_writer.h"
#include "core/logger.h"
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>

namespace hp::kernel {

SysfsWriter::SysfsWriter() {
    detect();
    for (int i = 0; i < 8; ++i) open(i);
}

SysfsWriter::~SysfsWriter() {
    for (auto& f : fds_) {
        if (f.mn >= 0) close(f.mn);
        if (f.mx >= 0) close(f.mx);
        if (f.um >= 0) close(f.um);
        if (f.ux >= 0) close(f.ux);
    }
}

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

bool SysfsWriter::open(int c) noexcept {
    if (c < 0 || c >= 8) return false;
    auto& f = fds_[c];
    char p[128];    
    snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", c);
    // 使用 ::open 确保调用 POSIX open
    f.mn = ::open(p, O_WRONLY);
    
    snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", c);
    f.mx = ::open(p, O_WRONLY);
    
    if (bk_ == Backend::UCLAMP) {
        snprintf(p, sizeof(p), "/dev/cpuctl/cpu%d/uclamp.min", c);
        f.um = ::open(p, O_WRONLY);
        snprintf(p, sizeof(p), "/dev/cpuctl/cpu%d/uclamp.max", c);
        f.ux = ::open(p, O_WRONLY);
    }
    return f.mn >= 0 && f.mx >= 0;
}

bool SysfsWriter::wf(int c, uint32_t mn, uint32_t mx) noexcept {
    auto& f = fds_[c];
    if (f.mn < 0 || f.mx < 0) return false;
    char buf_min[16], buf_max[16];  // 分离缓冲区，避免复用导致写入错误
    int l1 = snprintf(buf_min, sizeof(buf_min), "%u\n", mn);
    int l2 = snprintf(buf_max, sizeof(buf_max), "%u\n", mx);
    return write(f.mn, buf_min, l1) == l1 && write(f.mx, buf_max, l2) == l2;
}

bool SysfsWriter::wu(int c, uint8_t mn, uint8_t mx) noexcept {
    auto& f = fds_[c];
    if (f.um < 0 || f.ux < 0) return false;
    char buf_min[16], buf_max[16];  // 分离缓冲区
    int l1 = snprintf(buf_min, sizeof(buf_min), "%u\n", mn);
    int l2 = snprintf(buf_max, sizeof(buf_max), "%u\n", mx);
    return write(f.um, buf_min, l1) == l1 && write(f.ux, buf_max, l2) == l2;
}

bool SysfsWriter::wc(int c, uint8_t pct) noexcept {
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

bool SysfsWriter::apply(const std::vector<std::pair<int, FreqConfig>>& b) noexcept {
    bool ok = false;    for (const auto& [cpu, cfg] : b) {
        if (bk_ == Backend::UCLAMP) ok |= wu(cpu, cfg.uclamp_min, cfg.uclamp_max);
        else if (bk_ == Backend::CGROUPS) ok |= wc(cpu, cfg.uclamp_max);
        ok |= wf(cpu, cfg.min_freq, cfg.target_freq);
    }
    return ok;
}

} // namespace hp::kernel