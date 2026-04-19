#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace hp::device {

// 迁移策略配置
struct MigrationConfig {
    // 层级迁移阈值 (0-1024)
    uint32_t little_to_mid = 256;      // 小核→中核
    uint32_t mid_to_little = 240;       // 中核→小核
    uint32_t mid_to_big = 640;         // 中核→大核
    
    // 冷却期
    uint32_t little_cool = 6;
    uint32_t mid_cool = 6;
    uint32_t big_cool = 4;
    
    // 负载均衡
    uint32_t load_balance_min = 256;   // 最小负载均衡阈值
    float load_balance_threshold = 0.3f;  // 负载差异比例
    
    // 过载保护
    uint32_t overload_util = 768;      // 过载阈值
    uint32_t overload_rq = 4;          // 运行队列过载
    
    // 功率感知
    uint32_t high_power = 2000;        // 高功耗阈值 (mW)
    uint32_t low_power = 1000;         // 低功耗阈值 (mW)
};

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
    
    // 迁移策略配置
    MigrationConfig migration;      // 独立迁移参数
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