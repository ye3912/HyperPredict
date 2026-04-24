#include "core/boot_calibrator.h"
#include "device/cpu_topology.h"
#include "core/logger.h"
#include <algorithm>
#include <fstream>
#include <cstdio>

namespace hp {

bool BootCalibrator::calibrate(const device::CpuTopology& topo) noexcept {
    const auto& domains = topo.get_domains();
    if (domains.empty()) return false;
    
    // 读取实际频点列表
    auto read_freqs = [](int cpu_idx) -> std::vector<uint32_t> {
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies", cpu_idx);
        std::ifstream f(path);
        std::vector<uint32_t> freqs;
        uint32_t freq;
        while (f >> freq) freqs.push_back(freq);
        return freqs;
    };
    
    // 找到最高频域（prime/big）
    auto prime_it = std::max_element(domains.begin(), domains.end(),
        [](const auto& a, const auto& b) { return a.max_freq < b.max_freq; });
    
    // 找到最低频域（little）
    auto little_it = std::min_element(domains.begin(), domains.end(),
        [](const auto& a, const auto& b) { return a.min_freq < b.min_freq; });
    
    if (prime_it != domains.end() && little_it != domains.end()) {
        // 使用实际的频点列表
        std::vector<uint32_t> prime_freqs = read_freqs(prime_it->cpus[0]);
        std::vector<uint32_t> little_freqs = read_freqs(little_it->cpus[0]);
        
        if (!prime_freqs.empty()) {
            baseline_.big.target_freq = prime_freqs.front();  // max
            baseline_.big.min_freq = prime_freqs.back();        // min
            baseline_.big.steps = prime_freqs;                  // 所有频点
        } else {
            baseline_.big.target_freq = prime_it->max_freq;
            baseline_.big.min_freq = prime_it->min_freq;
        }
        
        if (!little_freqs.empty()) {
            baseline_.little.target_freq = little_freqs.front();
            baseline_.little.min_freq = little_freqs.back();
            baseline_.little.steps = little_freqs;
        } else {
            baseline_.little.target_freq = little_it->max_freq;
            baseline_.little.min_freq = little_it->min_freq;
        }
        
        LOGI("Calibrated: big=%u-%u (%zu steps), little=%u-%u (%zu steps)",
             baseline_.big.min_freq, baseline_.big.target_freq, baseline_.big.steps.size(),
             baseline_.little.min_freq, baseline_.little.target_freq, baseline_.little.steps.size());
        return true;
    }
    
    return false;
}

} // namespace hp