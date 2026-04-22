#include "device/migration_engine_v2.h"
#include "core/logger.h"
#include <algorithm>
#include <cmath>

namespace hp::device {

void MigrationEngineV2::init(const HardwareProfile& p) noexcept {
    prof_ = p;
    thermal_limit_ = static_cast<uint32_t>(p.thermal_limit);
    budget_ = find_power_budget(p.soc_name.c_str());
    
    // 初始化核心角色
    for (int i = 0; i < 8; i++) {
        cores_[i].cpu = i;
        cores_[i].util = 0;
        cores_[i].rq = 0;
        
        if (i < p.prime_cores) {
            cores_[i].role = CoreRole::Prime;
        } else if (i < p.prime_cores + p.big_cores) {
            cores_[i].role = CoreRole::Performance;
        } else {
            cores_[i].role = CoreRole::Little;
        }
    }
    
    active_cores_ = p.prime_cores + p.big_cores + p.little_cores;
    overutil_ratio_ = 0.0f;
    sample_count_ = 0;
    
    LOGI("MigrationEngineV2 initialized: prime=%u big=%u little=%u",
         p.prime_cores, p.big_cores, p.little_cores);
}

void MigrationEngineV2::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    
    cores_[cpu].util = util;
    cores_[cpu].rq = rq;
}

MigResult MigrationEngineV2::decide(int cur, uint32_t therm, bool game) noexcept {
    MigResult result{};
    sample_count_++;
    
    // 计算当前 EDP
    result.edp_cost = calc_total_edp();
    
    // Over-utilization 跟踪
    uint32_t total_util = 0;
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        total_util += cores_[i].util;
    }
    
    bool is_overutil = (total_util > active_cores_ * 870);  // 85%
    overutil_ratio_ = overutil_ratio_ * 0.9f + (is_overutil ? 0.1f : 0.0f);
    
    // 温控检查
    if (therm < thermal_limit_) {
        result.thermal = true;
        return result;
    }
    
    if (cur < 0 || cur >= 8) return result;
    
    const auto& cur_state = cores_[cur];
    
    // 小负载不需要迁移
    if (cur_state.util < 128) return result;
    
    // 查找最佳目标
    auto target = find_best_target(cur, cur_state);
    if (target && *target != cur) {
        result.target = *target;
        result.go = true;
        return result;
    }
    
    return result;
}

float MigrationEngineV2::calc_core_edp(const CoreState& core, float target_fps) const noexcept {
    if (core.util == 0) return 0.0f;
    
    // 计算功率
    uint32_t power;
    switch (core.role) {
        case CoreRole::Prime:
            power = budget_ ? budget_->prime_power_mw : 7000;
            break;
        case CoreRole::Performance:
            power = budget_ ? budget_->big_power_mw : 10800;
            break;
        case CoreRole::Little:
            power = budget_ ? budget_->little_power_mw : 3000;
            break;
        default:
            power = 5000;
    }
    
    // 实际 FPS (根据利用率估算)
    float util_norm = static_cast<float>(core.util) / 1024.0f;
    float fps = target_fps * util_norm;
    
    // EDP = power / fps²
    if (fps <= 0.0f) return 1e9f;
    return static_cast<float>(power) / (fps * fps);
}

float MigrationEngineV2::calc_total_edp() const noexcept {
    float total_edp = 0.0f;
    float target_fps = 60.0f;  // TODO: 从配置获取
    
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        if (cores_[i].util > 0) {
            total_edp += calc_core_edp(cores_[i], target_fps);
        }
    }
    
    return total_edp;
}

bool MigrationEngineV2::check_capacity(float total_util) const noexcept {
    uint32_t capacity = active_cores_ * 1024;
    return total_util <= capacity;
}

std::optional<int> MigrationEngineV2::find_best_target(int cur, const CoreState& cur_state) const noexcept {
    std::optional<int> best_target;
    float best_cost = 1e9f;
    
    CoreState best_state = cur_state;
    
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        if (i == cur) continue;
        
        const auto& target_core = cores_[i];
        
        // 目标核心利用率过高，跳过
        if (target_core.util > 768) continue;
        
        // 计算如果迁移到该核心的成本
        CoreState test_state = cur_state;
        test_state.cpu = i;
        test_state.role = target_core.role;
        
        float cost = calc_core_edp(test_state, 60.0f);
        
        // 小核更偏好
        if (target_core.role == CoreRole::Little) {
            cost *= 0.8f;  // 8折优惠
        }
        
        // 更新目标核心利用率
        float target_util_after = target_core.util + cur_state.util;
        if (target_util_after > 870) continue;  // 85% 过载检查
        
        if (cost < best_cost) {
            best_cost = cost;
            best_target = i;
            best_state = test_state;
        }
    }
    
    return best_target;
}

int MigrationEngineV2::solve_mmkp() noexcept {
    // MMKP 近似求解器
    // 选择总 EDP 最低的核组合
    
    float best_total_edp = 1e9f;
    int best_cpu = -1;
    
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        if (cores_[i].util > 870) continue;  // 过载核心不选
        
        float edp = calc_core_edp(cores_[i], 60.0f);
        
        // 小核优先
        if (cores_[i].role == CoreRole::Little) {
            edp *= 0.85f;
        }
        
        if (edp < best_total_edp) {
            best_total_edp = edp;
            best_cpu = i;
        }
    }
    
    return best_cpu;
}

float MigrationEngineV2::get_edp_cost() const noexcept {
    return calc_total_edp();
}

bool MigrationEngineV2::is_overutilized() const noexcept {
    return overutil_ratio_ > 0.3f;
}

const char* MigrationEngineV2::core_type_name(int cpu) const noexcept {
    if (cpu < 0 || cpu >= 8) return "Unknown";
    
    switch (cores_[cpu].role) {
        case CoreRole::Prime: return "Prime";
        case CoreRole::Performance: return "Performance";
        case CoreRole::Little: return "Little";
        default: return "Unknown";
    }
}

void MigrationEngineV2::reset() noexcept {
    for (auto& core : cores_) {
        core.util = 0;
        core.rq = 0;
        core.edp = 0.0f;
    }
    overutil_ratio_ = 0.0f;
    sample_count_ = 0;
}

} // namespace hp::device