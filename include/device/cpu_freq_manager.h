#pragma once
#include "device/cpu_topology.h"
#include <vector>
#include <algorithm>

namespace hp::device {
struct FreqInfo {
    uint32_t min{0};
    uint32_t max{0};
    std::vector<uint32_t> steps;
};

class FreqManager {
public:
    bool init(const CpuTopology& t) noexcept;
    const FreqInfo& info(int idx) const noexcept { return doms_[idx]; }
    
    uint32_t snap(uint32_t t, int idx) const noexcept {
        if (idx < 0 || idx >= doms_.size() || doms_[idx].steps.empty()) return t;
        auto& s = doms_[idx].steps;
        auto it = std::lower_bound(s.begin(), s.end(), t);
        if (it == s.begin()) return s[0];
        if (it == s.end()) return s.back();
        return (t - *(it-1) < *it - t) ? *(it-1) : *it;
    }
    
private:
    std::vector<FreqInfo> doms_;
};
}