#include "core/boot_calibrator.h"
#include "device/cpu_topology.h"
#include "core/logger.h"
#include <algorithm>

namespace hp {

bool BootCalibrator::calibrate(const device::CpuTopology& topo) noexcept {
    const auto& domains = topo.get_domains();
    if (domains.empty()) return false;
    
    auto prime_it = std::max_element(domains.begin(), domains.end(),
        [](const auto& a, const auto& b) { return a.max_freq < b.max_freq; });
    
    auto little_it = std::min_element(domains.begin(), domains.end(),
        [](const auto& a, const auto& b) { return a.min_freq < b.min_freq; });
    
    if (prime_it != domains.end() && little_it != domains.end()) {
        baseline_.big.target_freq = prime_it->max_freq;
        baseline_.big.min_freq = prime_it->min_freq;
        baseline_.little.target_freq = little_it->max_freq;
        baseline_.little.min_freq = little_it->min_freq;
        
        LOGI("Calibrated: big=%u-%u kHz, little=%u-%u kHz",
             baseline_.big.min_freq, baseline_.big.target_freq,
             baseline_.little.min_freq, baseline_.little.target_freq);
        return true;
    }
    
    return false;
}

} // namespace hp