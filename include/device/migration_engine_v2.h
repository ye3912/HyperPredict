#pragma once
#include "device/migration_engine.h"
#include "device/energy_model.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <chrono>

namespace hp::device {

// MigrationEngine V2 - E-Mapper 风格全局优化器 (增强版)
class MigrationEngineV2 {
public:
    void init(const HardwareProfile& p) noexcept;
    
    // 更新核心负载
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    
    // 重载版本：包含唤醒次数
    void update(int cpu, uint32_t util, uint32_t rq, uint32_t wakeups) noexcept {
        update(cpu, util, rq);
    }
    
    // E-Mapper 风格决策
    [[nodiscard]] MigResult decide(int cur, uint32_t therm, bool game) noexcept;
    
    // 重置
    void reset() noexcept;
    
    // 获取当前 EDP 成本
    [[nodiscard]] float get_edp_cost() const noexcept;
    
    // 查询是否过载
    [[nodiscard]] bool is_overutilized() const noexcept;
    
    // 查询核心类型
    [[nodiscard]] const char* core_type_name(int cpu) const noexcept;
    
    // ========== 移植功能 ==========
    
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
    // 核心角色
    enum class CoreRole2 { Prime, Performance, Little };
    
    // 核心状态
    struct CoreState {
        uint32_t util{0};
        uint32_t rq{0};
        CoreRole2 role{CoreRole2::Little};
        int cpu{-1};
        float edp{0.0f};
    };
    
    // 历史趋势缓存 (从原版移植)
    struct TrendData {
        uint32_t prev_util{0};
        std::chrono::steady_clock::time_point last_update;
        float velocity{0.0f};
    };
    std::array<TrendData, 8> trend_cache_{};
    
    // MMKP 求解器
    [[nodiscard]] int solve_mmkp() noexcept;
    
    // EDP 计算
    [[nodiscard]] float calc_core_edp(const CoreState& core, float target_fps) const noexcept;
    [[nodiscard]] float calc_total_edp() const noexcept;
    
    // 容量检查
    [[nodiscard]] bool check_capacity(float total_util) const noexcept;
    
    // 查找目标
    [[nodiscard]] std::optional<int> find_best_target(int cur, const CoreState& cur_state) const noexcept;
    
    // 更新趋势
    void update_trend(int cpu, uint32_t util) noexcept;
    
    HardwareProfile prof_;
    std::array<CoreState, 8> cores_;
    uint32_t active_cores_{0};
    uint32_t thermal_limit_{85};
    
    // 功耗预算
    const CorePowerBudget* budget_{nullptr};
    
    // 过载跟踪
    float overutil_ratio_{0.0f};
    uint32_t sample_count_{0};
    
    // ========== 移植成员 ==========
    MigPolicy policy_{MigPolicy::Balanced};
    uint8_t cool_{0};
    
    // 设备类型
    bool is_legacy_{false};
    bool is_all_big_{false};
    
    // 全大核配置
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
};

} // namespace hp::device