#pragma once
#include "device/cpu_topology.h"
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>

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
    
    // ✅ 优化: O(1) 频率查找表
    // LUT 映射 (freq >> 10) -> nearest step
    static constexpr uint32_t LUT_GRANULARITY = 1024;
    std::vector<uint32_t> lut;
    
    // 构建查找表
    void build_lut() noexcept {
        if (steps.empty()) return;
        uint32_t max_idx = (max_freq / LUT_GRANULARITY) + 1;
        lut.resize(max_idx + 1);
        
        for (uint32_t i = 0; i <= max_idx; ++i) {
            uint32_t freq = i * LUT_GRANULARITY;
            auto it = std::lower_bound(steps.begin(), steps.end(), freq);
            if (it == steps.begin()) {
                lut[i] = steps[0];
            } else if (it == steps.end()) {
                lut[i] = steps.back();
            } else {
                lut[i] = (freq - *(it-1) < *it - freq) ? *(it-1) : *it;
            }
        }
    }
    
    // O(1) snap using lookup table
    uint32_t fast_snap(uint32_t freq) const noexcept {
        if (lut.empty()) {
            // Fallback to binary search
            auto it = std::lower_bound(steps.begin(), steps.end(), freq);
            if (it == steps.begin()) return steps[0];
            if (it == steps.end()) return steps.back();
            return (freq - *(it-1) < *it - freq) ? *(it-1) : *it;
        }
        uint32_t idx = freq / LUT_GRANULARITY;
        if (idx >= lut.size()) return steps.empty() ? freq : steps.back();
        return lut[idx];
    }
};

class CpuFreqManager {
public:
    bool init() noexcept;
    uint32_t snap(uint32_t t, int idx) const noexcept;
    // ✅ 新增: O(1) 快速查找
    uint32_t fast_snap(uint32_t t, int idx) const noexcept;
    const std::vector<FreqDomain>& domains() const noexcept { return doms_; }
    uint32_t get_min(int idx) const noexcept;
    uint32_t get_max(int idx) const noexcept;
    
private:
    std::vector<FreqDomain> doms_;
};

} // namespace hp::device