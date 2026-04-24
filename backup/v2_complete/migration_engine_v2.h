#pragma once
#include "device/hardware_analyzer.h"
#include "core/logger.h"
#include "device/energy_model.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <chrono>
#include <string>

namespace hp::device {

// ========== V2 独立类型定义 ==========
struct MigResult {
    int target{-1};
    bool go{false};
    bool thermal{false};
};

enum class MigPolicy : uint8_t {
    Conservative,
    Balanced,
    Aggressive
};

enum class DeviceGen : uint8_t {
    Legacy,
    Modern,
    Flagship
};

// MigrationEngine V2 - E-Mapper/MMKP 风格全局优化器
// 独立实现，保留与 V1 相同的接口
class MigrationEngineV2 {
public:
    // 核心负载结构体
    struct CoreLoad {
        uint32_t util{0};
        uint32_t run_queue{0};
        uint32_t wakeups{0};
    };
    
    void init(const HardwareProfile& p) noexcept;
    
    // 更新核心负载
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    
    // 重载版本：包含唤醒次数
    void update(int cpu, uint32_t util, uint32_t rq, uint32_t wakeups) noexcept;

    // E-Mapper 风格决策
    [[nodiscard]] MigResult decide(int cur, uint32_t therm, bool game) noexcept;
    
    // 重置
    void reset() noexcept;
    
    // 获取当前 EDP 成本
    [[nodiscard]] float get_edp_cost() const noexcept;
    
    // 查询是否过载
    [[nodiscard]] bool is_overutilized() const noexcept;
    
    // 查询核心类型名称
    [[nodiscard]] const char* core_type_name(int cpu) const noexcept;
    
    // 负载趋势
    [[nodiscard]] float get_util_trend(int cpu) const noexcept;
    
    // 设备代数识别
    void detect_device_generation() noexcept;
    
    // 全大核优化配置
    void configure_all_big_optimization() noexcept;
    
    // 线程放置 (全大核设备)
    [[nodiscard]] int select_thread_placement(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept;
    
    // 策略设置
    void set_policy(MigPolicy p) noexcept { policy_ = p; }
    [[nodiscard]] MigPolicy policy() const noexcept { return policy_; }
    
    // 冷却期设置
    void set_cooling(uint8_t cool) noexcept { cool_ = cool; }
    
    // 重置统计
    void reset_stats() noexcept;

private:
    // MMKP EDP 计算
    [[nodiscard]] float calc_core_edp(int cpu, float target_fps) const noexcept;
    [[nodiscard]] float calc_total_edp() const noexcept;
    
    // 容量检查
    [[nodiscard]] bool check_capacity(uint32_t total_util) const noexcept;
    
    // 查找目标 (MMKP 风格)
    [[nodiscard]] std::optional<int> find_mmkp_target(int cur) const noexcept;
    
    // 功耗估算
    [[nodiscard]] uint32_t estimate_power_savings(int from_cpu, int to_cpu, uint32_t util) const noexcept;
    
    // 核心状态 (用于 MMKP)
    struct CoreMetrics {
        uint32_t util{0};
        uint32_t rq{0};
        float edp{0.0f};
        bool overutil{false};
    };
    std::array<CoreMetrics, 8> metrics_{};
    
    // 过载跟踪
    float overutil_ratio_{0.0f};
    uint32_t sample_count_{0};
    
    // 热限制
    uint32_t thermal_limit_{85};
    
    // 功耗预算
    const CorePowerBudget* budget_{nullptr};

    // ========== 复制的 V1 成员 ==========
    HardwareProfile prof_;
    std::array<CoreLoad, 8> loads_{};
    uint8_t cool_{0};

    MigPolicy policy_{MigPolicy::Balanced};

    static constexpr uint8_t COOL_THERMAL = 4;
    static constexpr uint8_t COOL_CONSERVATIVE = 12;
    static constexpr uint8_t COOL_BALANCED = 8;
    static constexpr uint8_t COOL_AGGRESSIVE = 4;

    struct TrendData {
        uint32_t prev_util{0};
        std::chrono::steady_clock::time_point last_update;
        float velocity{0.0f};
    };
    std::array<TrendData, 8> trend_cache_{};

    DeviceGen device_gen_{DeviceGen::Modern};
    bool is_legacy_{false};
    bool is_all_big_{false};
    
    struct AllBigConfig {
        bool enabled{false};
        bool has_prime_cores{false};
        uint8_t prime_count{0};
        uint8_t perf_count{0};
        float freq_ratio{1.0f};
        uint32_t low_util_thresh{256};
        uint32_t high_util_thresh{512};
        uint32_t migration_cool{4};
    } all_big_config_;

    [[nodiscard]] uint8_t get_cooling_period(bool thermal, bool game) const noexcept;
    [[nodiscard]] std::optional<int> find_best_cpu(CoreRole role, uint32_t max_rq) const noexcept;
    [[nodiscard]] bool should_migrate(float util_norm, uint32_t rq, bool game) const noexcept;
    void update_trend(int cpu, uint32_t util) noexcept;
    [[nodiscard]] std::optional<int> find_all_big_target(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept;
};

} // namespace hp::device