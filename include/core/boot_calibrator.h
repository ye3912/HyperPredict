#pragma once
#include "core/types.h"

namespace hp {

class BootCalibrator {
    BaselinePolicy baseline_{};
    
public:
    bool calibrate() noexcept { return true; }
    const BaselinePolicy& baseline() const noexcept { return baseline_; }
};

}