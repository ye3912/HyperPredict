#pragma once
#include "types.h"  // ✅ 改成相对路径，因为和 types.h 同目录

namespace hp {

class BootCalibrator {
    BaselinePolicy baseline_{};
    
public:
    bool calibrate() noexcept { return true; }
    const BaselinePolicy& baseline() const noexcept { return baseline_; }
};

} // namespace hp