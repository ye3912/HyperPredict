#include "core/boot_calibrator.h"
#include "device/cpu_topology.h"
#include <algorithm>

namespace hp {

void BootCalibrator::calibrate(const device::CpuTopology& topo) noexcept {
    const auto& big_cores = topo.get_big_cores();
    const auto& little_cores = topo.get_little_cores();
    const auto& all_cores = topo.get_all_cores();

    if(all_cores.empty()) return;

    uint32_t max_big_freq = 0;
    for(int id : big_cores) {
        for(const auto& c : all_cores) {
            if(c.id == id) max_big_freq = std::max(max_big_freq, c.max_freq);
        }
    }
    if(max_big_freq == 0) max_big_freq = 2600000;

    uint32_t target_big = max_big_freq * 85 / 100;
    uint32_t min_big = max_big_freq * 50 / 100;

    uint8_t big_mask = 0;
    for(int id : big_cores) {
        if(id < 8) big_mask |= (1 << id);
    }
    
    uint8_t little_mask = 0;
    for(int id : little_cores) {
        if(id < 8) little_mask |= (1 << id);
    }
    if(little_mask == 0) little_mask = ~big_mask;

    baseline_.big.target_freq = target_big;
    baseline_.big.min_freq = min_big;
    baseline_.big.cpuset_mask = big_mask;
    baseline_.big.uclamp_min = 50;
    baseline_.big.uclamp_max = 100;
    baseline_.big.ramp_ms = 15;

    baseline_.little.target_freq = 1000000;
    baseline_.little.min_freq = 600000;
    baseline_.little.cpuset_mask = little_mask;
    baseline_.little.uclamp_min = 10;
    baseline_.little.uclamp_max = 40;
    baseline_.little.ramp_ms = 30;
}

} // namespace hp