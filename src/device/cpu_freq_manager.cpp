#include "device/cpu_freq_manager.h"
#include <fstream>
#include <sstream>
#include <cstdio>

namespace hp::device {

bool FreqManager::init(const CpuTopology& t) noexcept {
    doms_.clear();
    for (const auto& d : t.get_domains()) {
        FreqInfo fi;
        fi.min = d.min_freq;
        fi.max = d.max_freq;
        char p[128];
        snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies", d.cpus[0]);
        std::ifstream f(p);
        if (f) {
            std::string l;
            std::getline(f, l);
            std::istringstream is(l);
            uint32_t v;
            while (is >> v) fi.steps.push_back(v);
        }
        std::sort(fi.steps.begin(), fi.steps.end());
        if (fi.steps.empty()) {
            for (uint32_t v = fi.min; v <= fi.max; v += 100000) {
                fi.steps.push_back(v);
            }
        }
        doms_.push_back(fi);
    }
    return !doms_.empty();
}

} // namespace hp::device