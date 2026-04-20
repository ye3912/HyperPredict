#include "device/cpu_topology.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <cstdio>
#include <cstring>

namespace hp::device {

uint32_t CpuTopology::readCpuCapacity(int cpu) noexcept {
    char path[128];
    
    // 1. 首先尝试读取 Linux 标准的 cpu_capacity (最准确)
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", cpu);
    std::ifstream f(path);
    if (f) {
        uint32_t capacity;
        if (f >> capacity) {
            return capacity;  // 典型值: 1024=最快, 397约38%, 等
        }
    }
    
    // 2. 备用: 从 frequency 估算 (基于典型 big.LITTLE 比例)
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    std::ifstream freq_f(path);
    if (freq_f) {
        uint32_t max_freq;
        if (freq_f >> max_freq) {
            // 归一化到 1024 (假设 3GHz ≈ 1024)
            return std::min(1024u, (max_freq * 340) / 1000);
        }
    }
    
    return 512;  // 默认中等容量
}

uint32_t CpuTopology::readCacheSize(int cpu) noexcept {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index0/size", cpu);

    std::ifstream f(path);
    if (f) {
        std::string size_str;
        if (f >> size_str) {
            // 解析大小字符串 (如 "32K", "1M")
            if (size_str.find("K") != std::string::npos) {
                return std::stoul(size_str);
            } else if (size_str.find("M") != std::string::npos) {
                return std::stoul(size_str) * 1024;
            }
        }
    }

    return 0;
}

bool CpuTopology::detect() noexcept {
    domains_.clear();
    total_ = 0;
    
    // 方法1: 按 cpu_capacity 分组 (Linux 官方方法)
    std::map<uint32_t, std::vector<int>> cap_groups;
    for (int i = 0; i < 16; ++i) {
        uint32_t cap = readCpuCapacity(i);
        if (cap > 0) {
            cap_groups[cap].push_back(i);
            total_++;
        }
    }
    
    // 如果没读到 cpu_capacity，备用方法2: 按 frequency 分组
    if (cap_groups.empty()) {
        std::map<uint32_t, std::vector<int>> freq_groups;
        for (int i = 0; i < 16; ++i) {
            char p[128];
            snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
            std::ifstream f(p);
            if (f) {
                uint32_t v;
                if (f >> v) {
                    freq_groups[v].push_back(i);
                    total_++;
                }
            }
        }
        for (auto& [freq, cpus] : freq_groups) {
            Domain d;
            d.cpus = std::move(cpus);
            d.max_freq = freq;
            d.capacity = readCpuCapacity(d.cpus[0]);
            if (d.capacity == 0) {
                d.capacity = std::min(1024u, (freq * 340) / 1000);
            }
            domains_.push_back(std::move(d));
        }
    } else {
        // 使用 cpu_capacity 分组
        for (auto& [cap, cpus] : cap_groups) {
            Domain d;
            d.cpus = std::move(cpus);
            d.max_freq = 0;
            d.capacity = cap;
            
            // 读取频率信息
            if (!d.cpus.empty()) {
                char p[128];
                snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", d.cpus[0]);
                std::ifstream f(p);
                if (f) f >> d.max_freq;
                
                snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", d.cpus[0]);
                std::ifstream mf(p);
                if (mf) mf >> d.min_freq;
                else d.min_freq = d.max_freq / 4;
            }
            
            domains_.push_back(std::move(d));
        }
    }

    // 按 capacity 降序排序
    std::sort(domains_.begin(), domains_.end(), [](const Domain& a, const Domain& b) {
        return a.capacity > b.capacity;
    });
    
    return total_ > 0;
}

} // namespace hp::device