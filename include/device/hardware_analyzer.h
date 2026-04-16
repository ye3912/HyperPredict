#pragma once
#include "device/cpu_topology.h"
#include <array>
#include <string>
#include <cstdint>

namespace hp::device {

enum class CoreRole { LITTLE = 0, MID = 1, BIG = 2, PRIME = 3 };
enum class BindMode { BALANCED, PERFORMANCE, POWERSAVE, GAME };

struct HardwareProfile {
    std::string soc_name{"Unknown"};
    int total_cores{0};
    std::array<CoreRole, 8> roles{};
    int sched_cpu{4};
    bool is_all_big{false};
    bool enable_lb{true};
    uint32_t mig_threshold{70};
    int32_t thermal_limit{90};         // ✅ 新增：温控阈值
    float fas_sensitivity{1.0f};       // ✅ 新增：FAS灵敏度
};

class HardwareAnalyzer {
public:
    bool analyze() noexcept;
    const HardwareProfile& profile() const noexcept { return prof_; }
    CoreRole role(int cpu) const noexcept { 
        return cpu < 0 || cpu >= 8 ? CoreRole::LITTLE : prof_.roles[cpu]; 
    }
    
private:
    HardwareProfile prof_;
};

} // namespace hp::device