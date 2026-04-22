#pragma once
#include "device/hardware_analyzer.h"
#include "device/energy_model.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace hp::device {

struct MigResult {
    int target{-1};
    bool go{false};
    bool thermal{false};
    float edp_cost{0.0f};  // EDP 成本
};

// MigrationEngine V2 - E-Mapper 风格全局优化器
class MigrationEngineV2 {
public:
    void init(const HardwareProfile& p) noexcept;
    
    // 更新核心负载
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    
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

private:
    // 核心角色
    enum class CoreRole { Prime, Performance, Little };
    
    // 核心状态
    struct CoreState {
        uint32_t util{0};
        uint32_t rq{0};
        CoreRole role{CoreRole::Little};
        int cpu{-1};
        float edp{0.0f};
    };
    
    // MMKP 求解器 - E-Mapper 核心算法
    [[nodiscard]] int solve_mmkp() noexcept;
    
    // 计算核心 EDP
    [[nodiscard]] float calc_core_edp(const CoreState& core, float target_fps) const noexcept;
    
    // 计算总 EDP
    [[nodiscard]] float calc_total_edp() const noexcept;
    
    // 容量约束检查
    [[nodiscard]] bool check_capacity(float total_util) const noexcept;
    
    // 查找最佳迁移目标
    [[nodiscard]] std::optional<int> find_best_target(int cur, const CoreState& cur_state) const noexcept;
    
    HardwareProfile prof_;
    std::array<CoreState, 8> cores_;
    uint32_t active_cores_{0};
    uint32_t thermal_limit_{85};
    
    // 功耗预算
    const CorePowerBudget* budget_{nullptr};
    
    // 过载跟踪
    float overutil_ratio_{0.0f};
    uint32_t sample_count_{0};
};

} // namespace hp::device