#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace hp::device {

struct SoCProfile {
    std::string name;              // 芯片名称
    std::string manufacturer;      // 制造商 (Qualcomm, MediaTek, Huawei, Samsung, Google)
    std::string architecture;      // CPU 架构 (ARMv8, ARMv9)
    std::string microarch;          // CPU 微架构 (Cortex-X4, Cortex-A720, etc.)
    std::vector<std::string> aliases;  // 别名列表
    uint8_t prime_cores;           // 超大核数量
    uint8_t big_cores;             // 大核数量
    uint8_t little_cores;          // 小核数量
    uint32_t max_freq_khz;         // 最高主频 (kHz)
    uint32_t min_freq_khz;         // 最低主频 (kHz) - 空闲时可下探到此频率
    int32_t thermal_limit;         // 触发温控的阈值 (度)
    float fas_sensitivity;         // FAS 调频灵敏度 (越大越激进)
    uint32_t mig_threshold;        // 核间迁移负载阈值 (0-1024)
    bool is_all_big;               // 是否全大核架构 (如 8 Elite/天玑9300)
};

class SoCDatabase {
    static std::unordered_map<std::string, SoCProfile> db;
    static std::unordered_map<std::string, std::string> device_map;  // 设备型号 -> SoC ID
    static bool loaded;

public:
    // 加载数据库（内部自动调用）
    static bool load() noexcept;

    // 查找芯片配置（支持精确/前缀/代号模糊匹配）
    static const SoCProfile* find(const std::string& id) noexcept;

    // 通过设备型号查找 SoC
    static const SoCProfile* findByDevice(const std::string& device) noexcept;

    // 获取所有支持的 SoC 列表
    static std::vector<std::string> getAllSoCs() noexcept;
};

} // namespace hp::device