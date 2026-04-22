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
    std::string manufacturer{"Unknown"};      // 制造商
    std::string architecture{"Unknown"};     // CPU 架构
    std::string microarch{"Unknown"};         // CPU 微架构
    std::string device_model{"Unknown"};      // 设备型号
    int total_cores{0};
    std::array<CoreRole, 8> roles{};
    int sched_cpu{4};
    bool is_all_big{false};
    bool enable_lb{true};
    uint32_t mig_threshold{70};
    int32_t thermal_limit{90};         // ✅ 新增：温控阈值
    float fas_sensitivity{1.0f};       // ✅ 新增：FAS灵敏度
    uint32_t min_freq_khz{300000};    // ✅ 新增：最低频率 (kHz) - 空闲时可下探到此频率
    uint32_t max_freq_khz{3300000};   // ✅ 新增：最高频率 (kHz)
    uint8_t prime_cores{0};           // ✅ 新增：超大核数量
    uint8_t big_cores{0};              // ✅ 新增：大核数量
    uint8_t little_cores{0};          // ✅ 新增：小核数量
    MigrationConfig migration{};         // ✅ 新增：迁移策略配置
    DailyConfig daily{};                 // ✅ 新增：日常调频配置
    VideoConfig video{};                // ✅ 新增：视频调频配置
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