#include "device/cpu_topology.h"
#include "core/logger.h"
#include <fstream>
#include <algorithm>
#include <dirent.h>
#include <cstring>

namespace hp::device {

bool CpuTopology::detect() noexcept {
    cores_.clear();
    big_cores_.clear();
    little_cores_.clear();
    highest_freq_core_ = -1;

    DIR* dir = opendir("/sys/devices/system/cpu");
    if(!dir) return false;

    struct dirent* entry;
    while((entry = readdir(dir)) != nullptr) {
        if(strncmp(entry->d_name, "cpu", 3) == 0) {
            int id = atoi(entry->d_name + 3);
            CPUCore core{id, read_max_freq(id), 0, is_cpu_online(id)};
            if(core.is_online && core.max_freq > 0) {
                cores_.push_back(core);
                if(highest_freq_core_ < 0 || 
                   core.max_freq > cores_[highest_freq_core_].max_freq) {
                    highest_freq_core_ = static_cast<int>(cores_.size()) - 1;
                }
            }
        }
    }
    closedir(dir);

    if(cores_.empty()) return false;

    classify_cores();
    print_info();
    return true;
}

void CpuTopology::classify_cores() noexcept {
    if(cores_.empty()) return;

    uint64_t sum = 0;
    for(const auto& c : cores_) sum += c.max_freq;
    uint32_t avg = sum / cores_.size();
    uint32_t threshold = avg * 80 / 100;

    for(size_t i = 0; i < cores_.size(); ++i) {
        if(cores_[i].max_freq >= threshold) {
            big_cores_.push_back(cores_[i].id);
        } else {
            little_cores_.push_back(cores_[i].id);
        }
    }
    
    if(big_cores_.empty() && highest_freq_core_ >= 0) {
        big_cores_.push_back(cores_[highest_freq_core_].id);
    }
}

uint32_t CpuTopology::read_max_freq(int cpu) noexcept {
    std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + 
                       "/cpufreq/scaling_max_freq";
    std::ifstream f(path);
    uint32_t freq = 0;
    return (f >> freq) ? freq : 0;
}

bool CpuTopology::is_cpu_online(int cpu) noexcept {
    std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/online";
    std::ifstream f(path);
    int val = 1;
    f >> val;
    return val != 0;
}

void CpuTopology::print_info() const noexcept {
    LOGI("Topology: %zu cores. Big: %zu, Little: %zu. Prime: CPU%d",
         cores_.size(), big_cores_.size(), little_cores_.size(),
         highest_freq_core_ >= 0 ? cores_[highest_freq_core_].id : -1);
    for(const auto& c : cores_) {
        LOGI("  CPU%d: %u MHz", c.id, c.max_freq / 1000);
    }
}

} // namespace hp::device
