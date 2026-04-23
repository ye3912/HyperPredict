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
            cores_[i].role = CoreRole2::Prime;
        } else if (i < p.prime_cores + p.big_cores) {
            cores_[i].role = CoreRole2::Performance;
        } else {
            cores_[i].role = CoreRole2::Little;
        }
    }
    
    active_cores_ = p.prime_cores + p.big_cores + p.little_cores;
    overutil_ratio_ = 0.0f;
    sample_count_ = 0;
    
    // 移植: 设备检测和全大核优化
    detect_device_generation();
    configure_all_big_optimization();
    
    LOGI("MigrationEngineV2 initialized: prime=%u big=%u little=%u (all_big=%d)",
         p.prime_cores, p.big_cores, p.little_cores, is_all_big_);
}

void MigrationEngineV2::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    
    cores_[cpu].util = util;
    cores_[cpu].rq = rq;
    
    // 移植: 更新趋势
    update_trend(cpu, util);
}

void MigrationEngineV2::reset() noexcept {
    for (auto& core : cores_) {
        core.util = 0;
        core.rq = 0;
        core.edp = 0.0f;
    }
    for (auto& trend : trend_cache_) {
        trend.prev_util = 0;
        trend.velocity = 0.0f;
    }
    overutil_ratio_ = 0.0f;
    sample_count_ = 0;
}

MigResult MigrationEngineV2::decide(int cur, uint32_t therm, bool game) noexcept {
    MigResult result{};
    sample_count_++;
    
    // Over-utilization 跟踪
    uint32_t total_util = 0;
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        total_util += cores_[i].util;
    }
    
    bool is_overutil = (total_util > active_cores_ * 870);
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
    
    // 移植: 冷却期检查
    if (cool_ > 0) {
        cool_--;
        return result;
    }
    
    // 全大核设备专用逻辑
    if (is_all_big_) {
        int placement = select_thread_placement(cur, cur_state.util, cur_state.rq, game);
        if (placement >= 0 && placement != cur) {
            result.target = placement;
            result.go = true;
            return result;
        }
    }
    
    // MMKP EDP 决策
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
    
    uint32_t power;
    switch (core.role) {
        case CoreRole2::Prime:
            power = budget_ ? budget_->prime_power_mw : 7000;
            break;
        case CoreRole2::Performance:
            power = budget_ ? budget_->big_power_mw : 10800;
            break;
        case CoreRole2::Little:
            power = budget_ ? budget_->little_power_mw : 3000;
            break;
        default:
            power = 5000;
    }
    
    float util_norm = static_cast<float>(core.util) / 1024.0f;
    float fps = target_fps * util_norm;
    
    if (fps <= 0.0f) return 1e9f;
    return static_cast<float>(power) / (fps * fps);
}

float MigrationEngineV2::calc_total_edp() const noexcept {
    float total_edp = 0.0f;
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        if (cores_[i].util > 0) {
            total_edp += calc_core_edp(cores_[i], 60.0f);
        }
    }
    return total_edp;
}

bool MigrationEngineV2::check_capacity(float total_util) const noexcept {
    return total_util <= active_cores_ * 1024;
}

std::optional<int> MigrationEngineV2::find_best_target(int cur, const CoreState& cur_state) const noexcept {
    std::optional<int> best_target;
    float best_cost = 1e9f;
    
    CoreState best_state = cur_state;
    
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        if (i == cur) continue;
        
        const auto& target_core = cores_[i];
        
        if (target_core.util > 768) continue;
        
        CoreState test_state = cur_state;
        test_state.cpu = i;
        test_state.role = target_core.role;
        
        float cost = calc_core_edp(test_state, 60.0f);
        
        // 小核优先
        if (target_core.role == CoreRole2::Little) {
            cost *= 0.8f;
        }
        
        float target_util_after = target_core.util + cur_state.util;
        if (target_util_after > 870) continue;
        
        if (cost < best_cost) {
            best_cost = cost;
            best_target = i;
        }
    }
    
    return best_target;
}

int MigrationEngineV2::solve_mmkp() noexcept {
    float best_total_edp = 1e9f;
    int best_cpu = -1;
    
    for (int i = 0; i < static_cast<int>(active_cores_); i++) {
        if (cores_[i].util > 870) continue;
        
        float edp = calc_core_edp(cores_[i], 60.0f);
        
        if (cores_[i].role == CoreRole2::Little) {
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
        case CoreRole2::Prime: return "Prime";
        case CoreRole2::Performance: return "Performance";
        case CoreRole2::Little: return "Little";
        default: return "Unknown";
    }
}

// ========== 移植功能实现 ==========

void MigrationEngineV2::update_trend(int cpu, uint32_t util) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    
    auto& trend = trend_cache_[cpu];
    auto now = std::chrono::steady_clock::now();
    
    if (trend.prev_util > 0) {
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - trend.last_update).count();
        if (delta > 0) {
            float velocity = static_cast<float>(util - trend.prev_util) / delta;
            trend.velocity = trend.velocity * 0.7f + velocity * 0.3f;  // EMA
        }
    }
    
    trend.prev_util = util;
    trend.last_update = now;
}

float MigrationEngineV2::get_util_trend(int cpu) const noexcept {
    if (cpu < 0 || cpu >= 8) return 0.0f;
    return trend_cache_[cpu].velocity;
}

void MigrationEngineV2::detect_device_generation() noexcept {
    // 检测全大核架构
    is_all_big_ = prof_.is_all_big;
    
    // 检测老旧设备 (865及以前)
    std::string name = prof_.soc_name;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    
    if (name.find("865") != std::string::npos ||
        name.find("855") != std::string::npos ||
        name.find("888") != std::string::npos ||
        name.find("870") != std::string::npos) {
        is_legacy_ = true;
    }
}

void MigrationEngineV2::configure_all_big_optimization() noexcept {
    if (!is_all_big_) return;
    
    all_big_config_.enabled = true;
    all_big_config_.has_prime_cores = (prof_.prime_cores > 0);
    all_big_config_.prime_count = prof_.prime_cores;
    all_big_config_.perf_count = prof_.big_cores;
    all_big_config_.freq_ratio = static_cast<float>(prof_.max_freq_khz) / prof_.min_freq_khz;
    all_big_config_.low_util_thresh = 256;
    all_big_config_.high_util_thresh = 512;
    all_big_config_.migration_cool = 4;
}

int MigrationEngineV2::select_thread_placement(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept {
    if (!all_big_config_.enabled) return -1;
    
    // 超大核优先 (游戏场景)
    if (is_game && all_big_config_.has_prime_cores && prof_.prime_cores > 0) {
        for (int i = 0; i < static_cast<int>(prof_.prime_cores); i++) {
            if (cores_[i].util < all_big_config_.low_util_thresh) {
                return i;
            }
        }
    }
    
    // 性能核
    for (int i = prof_.prime_cores; i < static_cast<int>(prof_.prime_cores + prof_.big_cores); i++) {
        if (cores_[i].util < all_big_config_.low_util_thresh) {
            return i;
        }
    }
    
    // 没有空闲核，返回当前
    return cur;
}

void MigrationEngineV2::reset_stats() noexcept {
    for (auto& core : cores_) {
        core.util = 0;
        core.rq = 0;
    }
    for (auto& trend : trend_cache_) {
        trend.prev_util = 0;
        trend.velocity = 0.0f;
    }
    overutil_ratio_ = 0.0f;
    sample_count_ = 0;
    cool_ = 0;
}

} // namespace hp::device