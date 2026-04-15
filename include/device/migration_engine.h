#pragma once
#include "device/hardware_analyzer.h"
#include <array>

namespace hp::device {

struct MigResult {
    int target{-1};
    bool go{false};
    bool thermal{false};
};

class MigrationEngine {
public:
    void init(const HardwareProfile& p) noexcept { prof_ = p; }
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    MigResult decide(int cur, uint32_t therm, bool game) noexcept;
    void reset() noexcept { loads_.fill({0, 0}); cool_ = 0; }
    
private:
    HardwareProfile prof_;
    struct Load { uint32_t u{0}, r{0}; };
    std::array<Load, 8> loads_{};
    uint32_t cool_{0};
};

} // namespace hp::device