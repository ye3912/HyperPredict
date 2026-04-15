<<<<<<< HEAD
// include/device/cpu_freq_manager.h
#pragma once
#include "device/cpu_topology.h"
#include <vector>
#include <algorithm>
namespace hp::device {
struct FreqInfo { uint32_t min{0}, max{0}; std::vector<uint32_t> steps; };
class FreqManager {
public:
    bool init(const CpuTopology& t) noexcept;
    const FreqInfo& info(int idx) const noexcept { return doms_[idx]; }
    uint32_t snap(uint32_t t, int idx) const noexcept {
        if(idx<0||idx>=doms_.size()||doms_[idx].steps.empty()) return t;
        auto& s=doms_[idx].steps; auto it=std::lower_bound(s.begin(),s.end(),t);
        if(it==s.begin()) return s[0]; if(it==s.end()) return s.back();
        return (t-*(it-1)<*it-t)?*(it-1):*it;
    }
private: std::vector<FreqInfo> doms_;
};
}
=======
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
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
