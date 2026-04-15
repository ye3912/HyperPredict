#include "core/boot_calibrator.h"
#include "core/logger.h"
#include <algorithm>
#include <numeric>

namespace hp {

void BootCalibrator::calibrate(const device::CpuTopology& topo) noexcept {
    const auto& domains = topo.get_domains();
    if(domains.empty()) {
        LOGE("No CPU domains found");
        return;
    }

    // 找到最高频域（prime/big）和最低频域（little/efficiency）
    const device::CpuTopology::Domain* prime_domain = nullptr;
    const device::CpuTopology::Domain* eff_domain = nullptr;
    
    for(const auto& d : domains) {
        if(!prime_domain || d.max_freq > prime_domain->max_freq) {
            prime_domain = &d;
        }
        if(!eff_domain || d.max_freq < eff_domain->max_freq) {
            eff_domain = &d;
        }
    }

    if(prime_domain && eff_domain) {
        // 大核基线：最高频域的 70%
        baseline_.big.target_freq = static_cast<uint32_t>(prime_domain->max_freq * 0.7f);
        baseline_.big.min_freq = static_cast<uint32_t>(prime_domain->min_freq);
        baseline_.big.uclamp_max = 85;
        
        // 小核基线：最低频域的 60%
        baseline_.little.target_freq = static_cast<uint32_t>(eff_domain->max_freq * 0.6f);
        baseline_.little.min_freq = static_cast<uint32_t>(eff_domain->min_freq);
        baseline_.little.uclamp_max = 70;
        
        // 中核基线（如果有多个域）
        if(domains.size() >= 3) {
            // 找到中间频域
            uint32_t mid_freq = 0;
            for(const auto& d : domains) {
                if(d.max_freq > eff_domain->max_freq && d.max_freq < prime_domain->max_freq) {
                    if(d.max_freq > mid_freq) {
                        mid_freq = d.max_freq;
                        baseline_.mid.target_freq = static_cast<uint32_t>(d.max_freq * 0.65f);
                        baseline_.mid.min_freq = static_cast<uint32_t>(d.min_freq);
                        baseline_.mid.uclamp_max = 75;
                    }
                }
            }
        } else {
            // 只有两个域时，中核用大核的 80%
            baseline_.mid.target_freq = static_cast<uint32_t>(prime_domain->max_freq * 0.8f);
            baseline_.mid.min_freq = baseline_.little.min_freq;
            baseline_.mid.uclamp_max = 80;
        }
        
        LOGI("Baseline calibrated: big=%u kHz, mid=%u kHz, little=%u kHz",
             baseline_.big.target_freq, baseline_.mid.target_freq, baseline_.little.target_freq);
    }
}

} // namespace hp