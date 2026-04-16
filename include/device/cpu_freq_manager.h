#pragma once
#include "device/cpu_topology.h"
#include <vector>
#include <cstdint>
#include <string>

namespace hp::device {

struct FreqStep {
    uint32_t freq_khz{0};
    int32_t latency_us{0};
};

struct FreqDomain {
    std::vector<int> cpus;
    std::vector<uint32_t> steps;
    uint32_t min_freq{0};
    uint32_t max_freq{0};
    std::string gov{};
};

class CpuFreqManager {
public:
    bool init() noexcept;
    uint32_t snap(uint32_t t, int idx) const noexcept;
    const std::vector<FreqDomain>& domains() const noexcept { return doms_; }
    uint32_t get_min(int idx) const noexcept;
    uint32_t get_max(int idx) const noexcept;
    
private:
    std::vector<FreqDomain> doms_;
};

} // namespace hp::device