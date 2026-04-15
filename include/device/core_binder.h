#pragma once
#include "device/hardware_analyzer.h"
#include <sched.h>
#include <fstream>
#include <cstdio>

namespace hp::device {

class CoreBinder {
public:
    void init(const HardwareProfile& p) noexcept { prof_ = p; }
    
    void apply(BindMode m) noexcept {
        mode_ = m;
        if (m == BindMode::POWERSAVE) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] >= CoreRole::BIG) {
                    constrain(i, 300000, 1200000);
                }
            }
        } else if (m == BindMode::GAME || m == BindMode::PERFORMANCE) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::LITTLE) {
                    constrain(i, 300000, 800000);
                }
            }
        }
    }
    
    bool bind_sched() noexcept {
        cpu_set_t m;
        CPU_ZERO(&m);
        CPU_SET(prof_.sched_cpu, &m);
        return sched_setaffinity(0, sizeof(m), &m) == 0;
    }
    
    BindMode mode() const noexcept { return mode_; }
    
private:
    HardwareProfile prof_;
    BindMode mode_{BindMode::BALANCED};
    
    void constrain(int cpu, uint32_t min, uint32_t max) noexcept {
        char p[128];
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
        std::ofstream(p) << min;
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        std::ofstream(p) << max;
    }
};

} // namespace hp::device