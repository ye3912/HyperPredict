#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace hp::device {
class CpuTopology {
public:
    struct Domain {
        std::vector<int> cpus;
        uint32_t min_freq{0};
        uint32_t max_freq{0};
        uint32_t capacity{0};      // CPU 容量 (0-1024)
        uint32_t cache_size{0};    // 缓存大小 (KB)
    };

    bool detect() noexcept;
    const std::vector<Domain>& get_domains() const noexcept { return domains_; }
    int get_total_cpus() const noexcept { return total_; }

private:
    std::vector<Domain> domains_;
    int total_{0};

    // 新增：辅助方法
    uint32_t readCpuCapacity(int cpu) noexcept;
    uint32_t readCacheSize(int cpu) noexcept;
};
}