#pragma once
#include "core/types.h"
#include "device/cpu_topology.h"

namespace hp {

class BootCalibrator {
    BaselinePolicy baseline_{};
    
public:
    bool calibrate(const device::CpuTopology& topo) noexcept;
    const BaselinePolicy& baseline() const noexcept { return baseline_; }
};

} // namespace hp