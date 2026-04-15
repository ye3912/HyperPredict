<<<<<<< HEAD
// src/kernel/sysfs_writer.cpp
#include "kernel/sysfs_writer.h"
#include "core/logger.h"
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
namespace hp::kernel {
SysfsWriter::SysfsWriter() { detect(); for(int i=0;i<8;++i)open(i); }
SysfsWriter::~SysfsWriter() { for(auto&f:fds_){if(f.mn>=0)close(f.mn);if(f.mx>=0)close(f.mx);if(f.um>=0)close(f.um);if(f.ux>=0)close(f.ux);} }
void SysfsWriter::detect() noexcept {
    std::ifstream f("/dev/cpuctl/cpu0/uclamp.min"); if(f.good()){bk_=Backend::UCLAMP;LOGI("Backend: UCLAMP");return;}
    if(access("/sys/fs/cgroup/cgroup.subtree_control",F_OK)==0){cg_root_="/sys/fs/cgroup";bk_=Backend::CGROUPS;LOGI("Backend: CGROUPS_V2");return;}
    if(access("/sys/fs/cgroup/cpu",F_OK)==0){cg_root_="/sys/fs/cgroup/cpu";bk_=Backend::CGROUPS;LOGI("Backend: CGROUPS_V1");return;}
    bk_=Backend::FREQ; LOGW("Backend: FREQ_ONLY");
}
bool SysfsWriter::open(int c) noexcept {
    if(c<0||c>=8)return false; auto&f=fds_[c]; char p[128];
    snprintf(p,sizeof(p),"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq",c);f.mn=open(p,O_WRONLY);
    snprintf(p,sizeof(p),"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq",c);f.mx=open(p,O_WRONLY);
    if(bk_==Backend::UCLAMP){snprintf(p,sizeof(p),"/dev/cpuctl/cpu%d/uclamp.min",c);f.um=open(p,O_WRONLY);snprintf(p,sizeof(p),"/dev/cpuctl/cpu%d/uclamp.max",c);f.ux=open(p,O_WRONLY);}
    return f.mn>=0&&f.mx>=0;
}
bool SysfsWriter::wf(int c, uint32_t mn, uint32_t mx) noexcept {
    auto&f=fds_[c]; if(f.mn<0||f.mx<0)return false; char buf[16]; int l1=snprintf(buf,sizeof(buf),"%u\n",mn),l2=snprintf(buf,sizeof(buf),"%u\n",mx);
    return write(f.mn,buf,l1)==l1&&write(f.mx,buf,l2)==l2;
}
bool SysfsWriter::wu(int c, uint8_t mn, uint8_t mx) noexcept {
    auto&f=fds_[c]; if(f.um<0||f.ux<0)return false; char buf[16]; int l1=snprintf(buf,sizeof(buf),"%u\n",mn),l2=snprintf(buf,sizeof(buf),"%u\n",mx);
    return write(f.um,buf,l1)==l1&&write(f.ux,buf,l2)==l2;
}
bool SysfsWriter::wc(int c, uint8_t pct) noexcept {
    if(cg_root_.empty())return false; char p[256];
    if(access((cg_root_+"/cgroup.subtree_control").c_str(),F_OK)==0){snprintf(p,sizeof(p),"%s/system/cpu%d/cpu.weight",cg_root_.c_str(),c);std::ofstream(p)<<(1+(pct*9999/100));}
    else{snprintf(p,sizeof(p),"%s/cpu%d/cpu.shares",cg_root_.c_str(),c);std::ofstream(p)<<(2+(pct*262142/100));}
    return true;
}
bool SysfsWriter::apply(const std::vector<std::pair<int,FreqConfig>>& b) noexcept {
    bool ok=false; for(const auto&[cpu,cfg]:b){
        if(bk_==Backend::UCLAMP)ok|=wu(cpu,cfg.uclamp_min,cfg.uclamp_max);
        else if(bk_==Backend::CGROUPS)ok|=wc(cpu,cfg.uclamp_max);
        ok|=wf(cpu,cfg.min_freq,cfg.target_freq);
    }return ok;
}
}
=======
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
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
