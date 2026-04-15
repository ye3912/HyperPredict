#pragma once
#include <vector>
#include <cstdint>

namespace hp::device {
class CpuTopology {
public:
    struct Domain {
        std::vector<int> cpus;
        uint32_t min_freq{0};
        uint32_t max_freq{0};
    };
    
    bool detect() noexcept;
    const std::vector<Domain>& get_domains() const noexcept { return domains_; }
    int get_total_cpus() const noexcept { return total_; }
    
private:
    std::vector<Domain> domains_;
    int total_{0};
};
}