#pragma once
#include "core/types.h"
namespace hp {
class BootCalibrator {
    BaselinePolicy baseline_{};
    void quick_freq_sweep() noexcept;
public:
    BaselinePolicy calibrate() noexcept;
    const BaselinePolicy& baseline() const noexcept { return baseline_; }
};
}