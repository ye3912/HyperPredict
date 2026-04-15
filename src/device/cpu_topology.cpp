#include "device/cpu_topology.h"
#include <fstream>
#include <map>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace hp::device {

bool CpuTopology::detect() noexcept {
    domains_.clear();
    total_cpus_ = 0;
    std::map<uint32_t, std::vector<int>> freq_groups;

    for(int i = 0; i < 16; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        std::ifstream f(path);
        if(f) {
            uint32_t max_f;
            if(f >> max_f) {
                freq_groups[max_f].push_back(i);
                total_cpus_++;
            }
        }
    }

    if(total_cpus_ == 0) return false;

    for(auto& [max_f, cpus] : freq_groups) {
        Domain d;
        d.cpus = std::move(cpus);
        d.max_freq = max_f;
        
        char min_path[128];
        snprintf(min_path, sizeof(min_path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", d.cpus[0]);
        std::ifstream mf(min_path);
        if(mf) mf >> d.min_freq;
        else d.min_freq = max_f / 4;
        
        domains_.push_back(std::move(d));
    }
    return true;
}

} // namespace hp::device