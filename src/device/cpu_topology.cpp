#include "device/cpu_topology.h"
#include <fstream>
#include <map>
#include <cstdio>
#include <cstring>

namespace hp::device {

uint32_t CpuTopology::readCpuCapacity(int cpu) noexcept {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", cpu);

    std::ifstream f(path);
    if (f) {
        uint32_t capacity;
        if (f >> capacity) {
            return capacity;
        }
    }

    // 如果无法读取容量，根据频率估算
    return 0;
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
    std::map<uint32_t, std::vector<int>> g;

    for (int i = 0; i < 16; ++i) {
        char p[128];
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        std::ifstream f(p);
        if (f) {
            uint32_t v;
            if (f >> v) {
                g[v].push_back(i);
                total_++;
            }
        }
    }

    if (!total_) return false;

    for (auto& [mx, cpus] : g) {
        Domain d;
        d.cpus = std::move(cpus);
        d.max_freq = mx;

        // 读取最小频率
        char mp[128];
        snprintf(mp, sizeof(mp), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", d.cpus[0]);
        std::ifstream mf(mp);
        if (mf) mf >> d.min_freq;
        else d.min_freq = mx / 4;

        // 读取 CPU 容量
        d.capacity = readCpuCapacity(d.cpus[0]);

        // 读取缓存大小
        d.cache_size = readCacheSize(d.cpus[0]);

        domains_.push_back(std::move(d));
    }
    return true;
}

} // namespace hp::device