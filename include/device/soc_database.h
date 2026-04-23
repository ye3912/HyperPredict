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

    // 新增字段：EDP 驱动迁移优化
    float rq_penalty_factor = 1.0f;    // 运行队列惩罚因子
    int cache_domain[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // 缓存域分组（0-3 表示不同域）
    uint32_t migration_cost_us = 100;  // 迁移成本（微秒）
    float edp_rebalance_ratio = 0.15f; // EDP 重平衡比例
};

// 日常调频配置 (论文参考: 低负载时省电)
struct DailyConfig {
    uint8_t idle_util_thresh{80};       // idle 检测阈值 (0-1024)
    uint8_t idle_rq_thresh{1};          // idle 运行队列阈值
    uint8_t idle_touch_thresh{5};       // idle 触摸阈值
    uint8_t idle_uclamp_max{10};         // idle uclamp_max
    uint8_t daily_uclamp_max{50};        // 日常 uclamp_max
    uint8_t daily_util_thresh{100};         // 日常场景负载阈值
    uint32_t daily_freq_khz{300000};       // 日常基础频率
    // EMA 平滑权重 (日常更平滑，减少抖动)
    float short_alpha{0.10f};    // 10% 新值，更平滑
    float medium_alpha{0.20f};   // 20% 新值
    float long_alpha{0.90f};     // 90% 历史值
};

// 视频调频配置
struct VideoConfig {
    uint32_t video_freq_khz{300000};     // 视频基础频率
    uint8_t video_util_thresh{150};    // 视频场景负载阈值
    uint8_t video_uclamp_max{50};      // 视频 uclamp_max
    uint32_t video_rate_limit_us{20000};  // 视频调频限速 (20ms)
    // EMA 平滑权重 (视频中等平滑)
    float short_alpha{0.20f};    // 20% 新值
    float medium_alpha{0.40f};   // 40% 新值
    float long_alpha{0.80f};     // 80% 历史值
};

// Target FPS 档位枚举
enum class TargetFPS : uint8_t {
    FPS_30 = 30,
    FPS_60 = 60,
    FPS_90 = 90,
    FPS_120 = 120,
    FPS_144 = 144,
    FPS_165 = 165
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
    // 日常调频配置
    DailyConfig daily;
    // 视频调频配置
    VideoConfig video;
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