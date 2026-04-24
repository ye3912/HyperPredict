#pragma once
#include "device/cpu_topology.h"
#include "device/soc_database.h"
#include <array>
#include <string>
#include <cstdint>

namespace hp::device {

enum class CoreRole { LITTLE = 0, MID = 1, BIG = 2, PRIME = 3 };
enum class BindMode { BALANCED, PERFORMANCE, POWERSAVE, GAME };

struct HardwareProfile {
    std::string soc_name{"Unknown"};
    std::string manufacturer{"Unknown"};
    std::string architecture{"Unknown"};
    std::string microarch{"Unknown"};
    std::string device_model{"Unknown"};
    int total_cpus{0};
    std::array<CoreRole, 8> roles{};
    std::array<uint32_t, 8> freqs{};  // 每个 CPU 的最大频率
    int sched_cpu{4};
    bool is_all_big{false};
    bool enable_lb{true};
    uint32_t mig_threshold{70};
    int32_t thermal_limit{90};
    float fas_sensitivity{1.0f};
    uint32_t min_freq_khz{300000};
    uint32_t max_freq_khz{3300000};
    uint8_t prime_cores{0};
    uint8_t big_cores{0};
    uint8_t little_cores{0};
    MigrationConfig migration{};
    DailyConfig daily{};
    VideoConfig video{};
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

    // 新增：辅助方法
    std::string getSystemProperty(const char* prop) noexcept;
    std::string detectDeviceModel() noexcept;
    std::string detectCpuArchitecture() noexcept;
    std::string detectCpuMicroarch() noexcept;
};

} // namespace hp::device