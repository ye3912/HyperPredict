#pragma once
#include <cstdint>

namespace hp::device {

// E-Mapper 风格: 核心功耗预算
struct CorePowerBudget {
    uint32_t prime_power_mw{0};    // Prime 核峰值功耗
    uint32_t big_power_mw{0};      // Big 核峰值功耗
    uint32_t little_power_mw{0};   // Little 核峰值功耗
    uint32_t total_budget_mw{0};   // 总功耗预算
};

// 能量模型查询
const CorePowerBudget* find_power_budget(const char* soc_id) noexcept;

// EDP 成本计算 (越低越好)
float calc_edp_cost(uint32_t power_mw, float fps, float target_fps) noexcept;

} // namespace hp::device