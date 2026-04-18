#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

namespace hp::device {

struct SoCProfile {
    std::string name;              // 芯片名称
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
    static bool loaded;

public:
    // 加载数据库（内部自动调用）
    static bool load() noexcept;

    // 查找芯片配置（支持精确/前缀/代号模糊匹配）
    static const SoCProfile* find(const std::string& id) noexcept;
};

} // namespace hp::device