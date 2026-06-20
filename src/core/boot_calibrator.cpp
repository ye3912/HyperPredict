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
        
        // 校准 mid-cluster（三集群 SoC，如骁龙 8 Gen 2 的 1+3+4）
        if (domains.size() >= 3) {
            auto mid_it = std::find_if(domains.begin(), domains.end(),
                [&](const auto& d) {
                    return &d != &(*prime_it) && &d != &(*little_it);
                });
            if (mid_it != domains.end()) {
                baseline_.mid.target_freq = mid_it->max_freq;
                baseline_.mid.min_freq = mid_it->min_freq;
            }
        }
        
        LOGI("Calibrated: big=%u-%u kHz, little=%u-%u kHz, mid=%u-%u kHz",
             baseline_.big.min_freq, baseline_.big.target_freq,
             baseline_.little.min_freq, baseline_.little.target_freq,
             baseline_.mid.min_freq, baseline_.mid.target_freq);
        return true;
    }
    
    return false;
}

} // namespace hp