#include "core/boot_calibrator.h"
#include "device/hardware.h"
#include <cmath>
#include <algorithm>

namespace hp {
void BootCalibrator::quick_freq_sweep() noexcept {
    FreqKHz freqs[] = BASELINE_FREQS;
    double p1 = 0.8, p2 = 2.1, p3 = 4.5;
    baseline_.pwr = {
        (p3 - p1) / (std::pow((double)freqs[12], 3) - std::pow((double)freqs[2], 3)),
        0.004,
        (p1 - baseline_.pwr.a * std::pow((double)freqs[2], 3)) / (double)freqs[2]
    };
}

BaselinePolicy BootCalibrator::calibrate() noexcept {
    quick_freq_sweep();
    baseline_.little = {600000, 1200000, 150, 0x0F, 10, 40, 30};
    baseline_.mid    = {1200000, 1800000, 180, 0x30, 30, 70, 25};
    baseline_.big    = {1800000, 2600000, 220, 0xC0, 50, 100, 15};
    baseline_.global_uclamp_max = 85;
    return baseline_;
}
} // namespace hp