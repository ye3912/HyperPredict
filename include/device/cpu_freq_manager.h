#pragma once
#include "device/cpu_topology.h"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace hp::device {

class CpuFreqManager {
public:
    struct DomainFreqInfo {
        uint32_t min_freq{0};
        uint32_t max_freq{0};
        std::vector<uint32_t> steps;
    };

    bool init(const CpuTopology& topo) noexcept;
    const DomainFreqInfo& get_domain_info(int domain_idx) const noexcept { return domains_[domain_idx]; }
    int get_domain_count() const noexcept { return domains_.size(); }
    uint32_t snap_to_step(uint32_t target, int domain_idx) const noexcept;

private:
    std::vector<DomainFreqInfo> domains_;
};

} // namespace hp::device