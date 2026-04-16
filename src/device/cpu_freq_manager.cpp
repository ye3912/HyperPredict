#include "device/cpu_freq_manager.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace hp::device {

bool CpuFreqManager::init() noexcept {
    doms_.clear();
    
    // 检测 CPU 拓扑
    char path[128];
    int cpu_idx = 0;
    
    while (cpu_idx < 8) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies", cpu_idx);
        
        FILE* fp = fopen(path, "r");
        if (!fp) {
            cpu_idx++;
            continue;
        }
        
        char line[1024] = {0};
        if (fgets(line, sizeof(line), fp)) {
            FreqDomain domain;
            std::istringstream iss(line);
            uint32_t freq;
            
            while (iss >> freq) {
                domain.steps.push_back(freq);
                if (domain.steps.empty() || freq < domain.min_freq) {
                    domain.min_freq = freq;
                }
                if (freq > domain.max_freq) {
                    domain.max_freq = freq;
                }
            }
            
            // 检查是否已存在相同频率域的 CPU
            bool found = false;
            for (auto& d : doms_) {
                if (d.steps == domain.steps) {
                    d.cpus.push_back(cpu_idx);
                    found = true;
                    break;                }
            }
            
            if (!found) {
                domain.cpus.push_back(cpu_idx);
                
                // 读取 governor
                snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu_idx);
                FILE* gov_fp = fopen(path, "r");
                if (gov_fp) {
                    char gov[64] = {0};
                    if (fgets(gov, sizeof(gov), gov_fp)) {
                        gov[strcspn(gov, "\r\n")] = 0;
                        domain.gov = gov;
                    }
                    fclose(gov_fp);
                }
                
                doms_.push_back(std::move(domain));
            }
        }
        
        fclose(fp);
        cpu_idx++;
    }
    
    // 按最高频率排序
    std::sort(doms_.begin(), doms_.end(), [](const FreqDomain& a, const FreqDomain& b) {
        return a.max_freq > b.max_freq;
    });
    
    LOGI("CpuFreqManager: %zu domains initialized", doms_.size());
    for (size_t i = 0; i < doms_.size(); ++i) {
        LOGI("  Domain %zu: CPUs=%zu, Freq=%u-%u kHz, Gov=%s",
             i, doms_[i].cpus.size(), doms_[i].min_freq, doms_[i].max_freq, doms_[i].gov.c_str());
    }
    
    return !doms_.empty();
}

// ✅ 修复：显式转换 idx 为 size_t
uint32_t CpuFreqManager::snap(uint32_t t, int idx) const noexcept {
    // 边界检查：修复有符号/无符号比较
    if (idx < 0 || static_cast<size_t>(idx) >= doms_.size()) {
        return t;
    }
    
    const auto& domain = doms_[idx];
    if (domain.steps.empty()) {
        return t;    }
    
    // 边界情况
    if (t <= domain.steps.front()) return domain.steps.front();
    if (t >= domain.steps.back()) return domain.steps.back();
    
    // 二分查找最近的频率
    size_t left = 0;
    size_t right = domain.steps.size() - 1;
    
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (domain.steps[mid] < t) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    return domain.steps[left];
}

uint32_t CpuFreqManager::get_min(int idx) const noexcept {
    if (idx < 0 || static_cast<size_t>(idx) >= doms_.size()) return 0;
    return doms_[idx].min_freq;
}

uint32_t CpuFreqManager::get_max(int idx) const noexcept {
    if (idx < 0 || static_cast<size_t>(idx) >= doms_.size()) return 0;
    return doms_[idx].max_freq;
}

} // namespace hp::device