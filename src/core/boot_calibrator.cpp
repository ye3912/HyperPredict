#include "core/boot_calibrator.h"
#include "device/hardware.h"
#include <cmath>
#include <algorithm>

namespace hp {

void BootCalibrator::quick_freq_sweep() noexcept {
    FreqKHz freqs[] = BASELINE_FREQS;
    double p1 = 0.8, p2 = 2.1, p3 = 4.5;
    baseline_.pwr = {
        (p3 - p1) / (std::pow(static_cast<double>(freqs[12]), 3) - 
                     std::pow(static_cast<double>(freqs[2]), 3)),
        0.004,
        (p1 - baseline_.pwr.a * std::pow(static_cast<double>(freqs[2]), 3)) / 
         static_cast<double>(freqs[2])
    };
}

BaselinePolicy BootCalibrator::calibrate() noexcept {
    quick_freq_sweep();
    
    baseline_.little.min_freq = 600000;
    baseline_.little.target_freq = 1200000;
    baseline_.little.boost_duration_ms = 150;
    baseline_.little.cpuset_mask = 0x0F;
    baseline_.little.uclamp_min = 10;
    baseline_.little.uclamp_max = 40;
    baseline_.little.ramp_ms = 30;
    baseline_.little.config_hash = 0x0F;
    
    baseline_.mid.min_freq = 1200000;
    baseline_.mid.target_freq = 1800000;
    baseline_.mid.boost_duration_ms = 180;
    baseline_.mid.cpuset_mask = 0x30;
    baseline_.mid.uclamp_min = 30;
    baseline_.mid.uclamp_max = 70;
    baseline_.mid.ramp_ms = 25;
    baseline_.mid.config_hash = 0x30;
    
    baseline_.big.min_freq = 1800000;
    baseline_.big.target_freq = 2600000;
    baseline_.big.boost_duration_ms = 220;
    baseline_.big.cpuset_mask = 0xC0;
    baseline_.big.uclamp_min = 50;
    baseline_.big.uclamp_max = 100;
    baseline_.big.ramp_ms = 15;
    baseline_.big.config_hash = 0xC0;
    
    baseline_.global_uclamp_max = 85;
    return baseline_;
}

} // namespace hp
