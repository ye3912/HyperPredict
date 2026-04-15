#include "device/cpu_freq_manager.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace hp::device {

bool CpuFreqManager::init(const CpuTopology& topo) noexcept {
    domains_.clear();
    for(const auto& dom : topo.get_domains()) {
        DomainFreqInfo info;
        info.min_freq = dom.min_freq;
        info.max_freq = dom.max_freq;
        
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies", dom.cpus[0]);
        std::ifstream f(path);
        if(f) {
            std::string line; 
            std::getline(f, line);
            std::istringstream iss(line);
            uint32_t freq;
            while(iss >> freq) info.steps.push_back(freq);
        }
        
        std::sort(info.steps.begin(), info.steps.end());
        if(info.steps.empty()) {
            for(uint32_t f = info.min_freq; f <= info.max_freq; f += 100000) 
                info.steps.push_back(f);
        }
        domains_.push_back(info);
    }
    return !domains_.empty();
}

uint32_t CpuFreqManager::snap_to_step(uint32_t target, int domain_idx) const noexcept {
    if(domain_idx < 0 || domain_idx >= static_cast<int>(domains_.size())) return target;
    const auto& steps = domains_[domain_idx].steps;
    if(steps.empty()) return target;
    
    auto it = std::lower_bound(steps.begin(), steps.end(), target);
    if(it == steps.begin()) return steps[0];
    if(it == steps.end()) return steps.back();
    return (target - *(it-1) < *it - target) ? *(it-1) : *it;
}

} // namespace hp::device