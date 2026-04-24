#include "device/migration_engine_v2.h"
#include "core/logger.h"
#include <algorithm>
#include <cmath>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace hp::device {

// ========== 任务分类 (基于 E-Mapper 论文) ==========
// TaskType 已定义在头文件中

// 任务分类参数
namespace TaskThresh {
    static constexpr uint32_t COMPUTE_INTENSIVE = 512;  // 50%
    static constexpr uint32_t MEMORY_INTENSIVE = 256;   // 25%
    static constexpr uint32_t HIGH_POWER = 2200;
    static constexpr uint32_t LOW_POWER = 1200;
}

static TaskType classify_task(uint32_t util, uint32_t run_queue, uint32_t wakeups) noexcept {
    if (util > TaskThresh::COMPUTE_INTENSIVE && run_queue < 2 && wakeups < 10) {
        return TaskType::COMPUTE_INTENSIVE;
    }
    if (util > TaskThresh::MEMORY_INTENSIVE && run_queue >= 2 && run_queue < 6 && wakeups >= 10) {
        return TaskType::MEMORY_INTENSIVE;
    }
    if (util < 256 && run_queue >= 4 && wakeups >= 20) {
        return TaskType::IO_INTENSIVE;
    }
    if (util < 128 && run_queue < 2) {
        return TaskType::IO_INTENSIVE;
    }
    return TaskType::UNKNOWN;
}

// 动态阈值计算
static uint32_t calc_dynamic_threshold(uint32_t base, uint32_t therm, bool is_game) noexcept {
    int32_t adjust = 0;
    if (therm < 10) adjust += 32;
    else if (therm < 20) adjust += 16;
    else if (therm > 40) adjust -= 32;
    else if (therm > 30) adjust -= 16;
    if (is_game) adjust -= 32;
    return static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(base) + adjust, 128, 896));
}

// 功率感知阈值
static uint32_t calc_power_aware_threshold(uint32_t base, uint32_t power_mw) noexcept {
    int32_t adjust = 0;
    if (power_mw > TaskThresh::HIGH_POWER) {
        adjust = static_cast<int32_t>(power_mw - TaskThresh::HIGH_POWER) / 100;
    } else if (power_mw < TaskThresh::LOW_POWER) {
        adjust = -static_cast<int32_t>(TaskThresh::LOW_POWER - power_mw) / 100;
    }
    return static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(base) + adjust, 128, 896));
}

// 动态冷却期
static uint32_t calc_dynamic_cooling(uint32_t base, uint32_t util, uint32_t target_util) noexcept {
    if (target_util < util - 128) return std::max(2u, base / 2);
    if (target_util > util - 64) return base * 2;
    return base;
}

// 按任务类型选择目标核心
static int select_target_by_task_type_v2(TaskType task_type, 
    const std::array<MigrationEngineV2::CoreLoad, 8>& loads,
    const std::array<CoreRole, 8>& roles, bool is_all_big) noexcept {
    
    int best = -1;
    uint32_t best_score = 0;

    for (int i = 0; i < 8; i++) {
        uint32_t score = 0;
        uint32_t util = loads[i].util;
        uint32_t rq = loads[i].run_queue;

        switch (task_type) {
            case TaskType::COMPUTE_INTENSIVE:
                if (is_all_big) {
                    if (roles[i] == CoreRole::PRIME) score = (1024 - util) + (8 - rq) * 64 + 256;
                    else if (roles[i] >= CoreRole::BIG) score = (1024 - util) + (8 - rq) * 64;
                } else {
                    if (roles[i] >= CoreRole::BIG) score = (1024 - util) + (8 - rq) * 64;
                }
                break;
            case TaskType::MEMORY_INTENSIVE:
                if (is_all_big) {
                    if (roles[i] >= CoreRole::BIG && roles[i] != CoreRole::PRIME) score = (1024 - util) + (8 - rq) * 64 + 128;
                } else {
                    if (roles[i] == CoreRole::MID) score = (1024 - util) + (8 - rq) * 64 + 128;
                }
                break;
            case TaskType::IO_INTENSIVE:
                if (is_all_big) {
                    if (roles[i] >= CoreRole::BIG && roles[i] != CoreRole::PRIME) score = (1024 - util) + (8 - rq) * 64 + 128;
                } else {
                    if (roles[i] == CoreRole::LITTLE) score = (1024 - util) + (8 - rq) * 64 + 128;
                }
                break;
            case TaskType::UNKNOWN:
                if (is_all_big) {
                    if (roles[i] >= CoreRole::BIG && roles[i] != CoreRole::PRIME) score = (1024 - util) + (8 - rq) * 64;
                } else {
                    if (roles[i] == CoreRole::MID) score = (1024 - util) + (8 - rq) * 64;
                }
                break;
        }

        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

// 计算中核平均利用率
static uint32_t calc_mid_avg_util(const std::array<MigrationEngineV2::CoreLoad, 8>& loads,
                                   const std::array<CoreRole, 8>& roles) noexcept {
    uint32_t total = 0, count = 0;
    for (int i = 0; i < 8; i++) {
        if (roles[i] == CoreRole::MID) {
            total += loads[i].util;
            count++;
        }
    }
    return count > 0 ? total / count : 0;
}

// ========== 主实现 ==========

void MigrationEngineV2::init(const HardwareProfile& p) noexcept {
    prof_ = p;
    loads_.fill({});
    reset_stats();
    detect_device_generation();
    configure_all_big_optimization();
    
    thermal_limit_ = static_cast<uint32_t>(p.thermal_limit);
    budget_ = find_power_budget(p.soc_name.c_str());
    
    for (int i = 0; i < 8; i++) {
        metrics_[i] = {};
    }
    
    overutil_ratio_ = 0.0f;
    sample_count_ = 0;
    
    LOGI("MigrationEngineV2 initialized: EDP + MMKP + TaskAware");
}

void MigrationEngineV2::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;

    // 添加历史数据
    util_history_[cpu].add(util);

    // 计算动态 alpha
    float mean_util = util_history_[cpu].get_mean();
    float std_dev = util_history_[cpu].get_stddev(mean_util);
    float alpha = std::clamp(0.2f + 0.6f * (std_dev / (mean_util + 0.01f)), 0.2f, 0.8f);

    auto& l = loads_[cpu];
    l.util = static_cast<uint32_t>(l.util * (1.0f - alpha) + util * alpha);
    l.run_queue = static_cast<uint32_t>(l.run_queue * 0.75f + rq * 0.25f);  // 乘法比除法快
    update_trend(cpu, util);

    metrics_[cpu].util = l.util;
    metrics_[cpu].rq = rq;
    metrics_[cpu].edp = calc_core_edp(cpu, 60.0f);
    metrics_[cpu].overutil = (l.util > 870);
}

void MigrationEngineV2::update(int cpu, uint32_t util, uint32_t rq, uint32_t wakeups) noexcept {
    if (cpu < 0 || cpu >= 8) return;

    // 添加历史数据
    util_history_[cpu].add(util);

    // 计算动态 alpha
    float mean_util = util_history_[cpu].get_mean();
    float std_dev = util_history_[cpu].get_stddev(mean_util);
    float alpha = std::clamp(0.2f + 0.6f * (std_dev / (mean_util + 0.01f)), 0.2f, 0.8f);

    auto& l = loads_[cpu];
    l.util = static_cast<uint32_t>(l.util * (1.0f - alpha) + util * alpha);
    l.run_queue = static_cast<uint32_t>(l.run_queue * 0.75f + rq * 0.25f);  // 乘法比除法快
    l.wakeups = static_cast<uint32_t>(l.wakeups * 0.75f + wakeups * 0.25f);  // 乘法比除法快
    update_trend(cpu, util);

    metrics_[cpu].util = l.util;
    metrics_[cpu].rq = rq;
    metrics_[cpu].edp = calc_core_edp(cpu, 60.0f);
    metrics_[cpu].overutil = (l.util > 870);
}

void MigrationEngineV2::reset() noexcept {
    loads_.fill({});
    cool_ = 0;
    reset_stats();
    for (auto& m : metrics_) m = {};
    overutil_ratio_ = 0.0f;
    sample_count_ = 0;
}

void MigrationEngineV2::update_trend(int cpu, uint32_t util) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    auto& trend = trend_cache_[cpu];
    auto now = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - trend.last_update).count();
    if (diff > 0) {
        trend.velocity = (static_cast<float>(util) - static_cast<float>(trend.prev_util)) / diff;
        trend.velocity = trend.velocity * 0.7f + trend.velocity * 0.3f;
    }
    trend.prev_util = util;
    trend.last_update = now;
}

float MigrationEngineV2::get_util_trend(int cpu) const noexcept {
    if (cpu < 0 || cpu >= 8) return 0.0f;
    return trend_cache_[cpu].velocity;
}

uint8_t MigrationEngineV2::get_cooling_period(bool thermal, bool game) const noexcept {
    if (thermal) return COOL_THERMAL;

    // 使用自适应冷却期
    return adaptive_cooling_.get_adaptive_cool();
}

std::optional<int> MigrationEngineV2::find_best_cpu(CoreRole role, uint32_t max_rq) const noexcept {
    std::optional<int> best;
    uint32_t best_score = 0;
    for (int i = 0; i < 8; i++) {
        if (prof_.roles[i] == role && loads_[i].run_queue < max_rq) {
            uint32_t score = (1024 - loads_[i].util);
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        }
    }
    return best;
}

bool MigrationEngineV2::should_migrate(float util_norm, uint32_t rq, bool game) const noexcept {
    if (game) return rq > 2 || util_norm > 0.5f;
    return rq > 4 || util_norm > 0.7f;
}

std::optional<int> MigrationEngineV2::find_all_big_target(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept {
    std::optional<int> target;
    uint32_t best_score = 0;
    for (int i = 0; i < 8; i++) {
        if (i == cur || loads_[i].util > 870) continue;
        uint32_t score = (1024 - loads_[i].util) + (8 - loads_[i].run_queue) * 64;
        if (is_game && prof_.roles[i] == CoreRole::PRIME) score += 256;
        if (score > best_score) {
            best_score = score;
            target = i;
        }
    }
    return target;
}

// ========== 功耗估算 ==========
uint32_t MigrationEngineV2::estimate_power_savings(int from_cpu, int to_cpu, uint32_t util) const noexcept {
    auto from_role = prof_.roles[from_cpu];
    auto to_role = prof_.roles[to_cpu];

    if (from_role <= CoreRole::MID && to_role >= CoreRole::BIG) {
        if (util > 512) return 200;
        return 0;
    }
    if (from_role >= CoreRole::BIG && to_role <= CoreRole::MID) {
        if (util < 256) return 1500;
        if (util < 384) return 1000;
        if (util < 512) return 500;
        return 200;
    }
    if (from_role == CoreRole::LITTLE && to_role == CoreRole::MID) {
        if (util > 320 && util < 512) return 800;
        if (util > 512) return 1200;
        return 400;
    }
    if (from_role == CoreRole::MID && to_role == CoreRole::LITTLE) {
        if (util < 192) return 1200;
        if (util < 256) return 800;
        return 400;
    }
    if (from_role == to_role) {
        uint32_t diff = std::abs(static_cast<int>(loads_[from_cpu].util) - static_cast<int>(loads_[to_cpu].util));
        if (diff > 256) return 600;
        if (diff > 128) return 300;
        return 100;
    }
    return 0;
}

MigResult MigrationEngineV2::decide(int cur, uint32_t therm, bool game, float target_fps) noexcept {
    MigResult result{};
    sample_count_++;
    
    // 保存 target_fps 供 EDP 计算使用
    if (target_fps > 0.0f) {
        target_fps_ = target_fps;
    }
    
    if (cur < 0 || cur >= 8) return result;
    
    // ========== 1. 温控紧急降级 ==========
    if (therm < 5) {
        result.thermal = true;
        result.go = true;
        for (int i = 0; i < 8; i++) {
            if (prof_.roles[i] <= CoreRole::MID) {
                result.target = i;
                break;
            }
        }
        cool_ = 6;
        return result;
    }
    
    // ========== 2. 冷却期检查 ==========
    if (cool_ > 0) {
        cool_--;
        return result;
    }
    
    uint32_t cur_util = loads_[cur].util;
    uint32_t cur_rq = loads_[cur].run_queue;
    CoreRole cur_role = prof_.roles[cur];
    
    // ========== 3. 任务分类 ==========
    TaskType task_type = classify_task(cur_util, cur_rq, loads_[cur].wakeups);
    uint32_t power_mw = 1800;  // 默认值
    
    // ========== 4. 游戏模式 ==========
    if (game) {
        if (cur_role < CoreRole::BIG) {
            for (int i = 7; i >= 0; --i) {
                if (prof_.roles[i] >= CoreRole::BIG && loads_[i].run_queue < 4) {
                    result.target = i;
                    result.go = true;
                    break;
                }
            }
        }
        cool_ = 2;
        return result;
    }
    
    // ========== 5. 老旧设备优化 (865等) ==========
    if (is_legacy_) {
        uint32_t mid_avg = calc_mid_avg_util(loads_, prof_.roles);
        uint32_t little_to_mid = calc_dynamic_threshold(256, therm, game);
        little_to_mid = calc_power_aware_threshold(little_to_mid, power_mw);
        uint32_t mid_to_little = calc_dynamic_threshold(240, therm, game);
        mid_to_little = calc_power_aware_threshold(mid_to_little, power_mw);
        uint32_t mid_to_big = calc_dynamic_threshold(640, therm, game);
        mid_to_big = calc_power_aware_threshold(mid_to_big, power_mw);
        
        // 负载趋势调整
        float trend = get_util_trend(cur);
        if (trend > 0.5f) {
            little_to_mid -= 32;
            mid_to_big -= 64;
        } else if (trend < -0.5f) {
            mid_to_little += 64;
        }
        
        // 小核过载保护
        if (cur_role == CoreRole::LITTLE && (cur_util > 768 || cur_rq >= 4)) {
            int best_mid = -1;
            uint32_t best_score = 0;
            for (int i = 0; i < 8; i++) {
                if (prof_.roles[i] == CoreRole::MID) {
                    uint32_t score = (1024 - loads_[i].util) - (loads_[i].run_queue * 128);
                    if (score > best_score) {
                        best_score = score;
                        best_mid = i;
                    }
                }
            }
            if (best_mid >= 0) {
                result.target = best_mid;
                result.go = true;
                cool_ = 6;
                return result;
            }
        }
        
        // LITTLE -> MID
        if (cur_role == CoreRole::LITTLE && cur_util > little_to_mid) {
            int target = select_target_by_task_type_v2(task_type, loads_, prof_.roles, false);
            if (target >= 0 && prof_.roles[target] == CoreRole::MID) {
                uint32_t save = estimate_power_savings(cur, target, cur_util);
                if (save > 250 || cur_util > little_to_mid + 32) {
                    result.target = target;
                    result.go = true;
                    cool_ = calc_dynamic_cooling(6, cur_util, loads_[target].util);
                    return result;
                }
            }
        }
        
        // MID -> LITTLE
        if (cur_role == CoreRole::MID && cur_util < mid_to_little && cur_rq < 2) {
            int best_little = -1;
            uint32_t min_util = cur_util;
            for (int i = 0; i < 8; i++) {
                if (prof_.roles[i] == CoreRole::LITTLE && loads_[i].util < min_util) {
                    min_util = loads_[i].util;
                    best_little = i;
                }
            }
            if (best_little >= 0) {
                result.target = best_little;
                result.go = true;
                cool_ = calc_dynamic_cooling(6, cur_util, min_util);
                return result;
            }
        }
        
        // MID -> BIG
        if (cur_role == CoreRole::MID && cur_util > mid_to_big) {
            int target = select_target_by_task_type_v2(task_type, loads_, prof_.roles, false);
            if (target >= 0 && prof_.roles[target] >= CoreRole::BIG) {
                uint32_t save = estimate_power_savings(cur, target, cur_util);
                if (save > 1000 || cur_util > mid_to_big + 64) {
                    result.target = target;
                    result.go = true;
                    cool_ = calc_dynamic_cooling(4, cur_util, loads_[target].util);
                    return result;
                }
            }
        }
    }
    
    // ========== 6. 全大核设备优化 ==========
    if (is_all_big_) {
        auto target = find_all_big_target(cur, cur_util, cur_rq, game);
        if (target.has_value()) {
            result.target = target.value();
            result.go = true;
            cool_ = all_big_config_.migration_cool;
            return result;
        }
        
        if (cur_util < all_big_config_.low_util_thresh && cur_rq < 2) {
            return result;
        }
        
        if (cur_util > all_big_config_.high_util_thresh || cur_rq > 2) {
            int target = select_target_by_task_type_v2(task_type, loads_, prof_.roles, true);
            if (target >= 0 && target != cur) {
                result.target = target;
                result.go = true;
                cool_ = all_big_config_.migration_cool;
                return result;
            }
        }
    }
    
    // ========== 7. 现代设备过载保护 ==========
    if (cur_role <= CoreRole::MID && (cur_util > 768 || cur_rq >= 4)) {
        int target = select_target_by_task_type_v2(task_type, loads_, prof_.roles, false);
        if (target >= 0 && prof_.roles[target] >= CoreRole::BIG) {
            result.target = target;
            result.go = true;
            cool_ = 4;
            return result;
        }
        // 回退：找最低负载的大核
        int best_big = -1;
        uint32_t min_load = UINT32_MAX;
        for (int i = 0; i < 8; i++) {
            if (prof_.roles[i] >= CoreRole::BIG) {
                uint32_t load = loads_[i].util + loads_[i].run_queue * 128;
                if (load < min_load) {
                    min_load = load;
                    best_big = i;
                }
            }
        }
        if (best_big >= 0) {
            result.target = best_big;
            result.go = true;
            cool_ = 4;
            return result;
        }
    }
    
    // ========== 8. 现代设备: 小核上浮 ==========
    if (cur_role <= CoreRole::MID && cur_util > 384) {
        int target = select_target_by_task_type_v2(task_type, loads_, prof_.roles, false);
        if (target >= 0 && loads_[target].util < cur_util) {
            result.target = target;
            result.go = true;
            cool_ = 4;
            return result;
        }
    }
    
    // ========== 9. MMKP EDP 优化 ==========
    if (cur_util >= 128) {
        auto mmkp_target = find_mmkp_target(cur);
        if (mmkp_target && *mmkp_target != cur) {
            // 计算 EDP 差值 (使用动态 target_fps_)
            float cur_edp = calc_core_edp(cur, target_fps_);
            float target_edp = calc_core_edp(*mmkp_target, target_fps_);
            float edp_diff = (cur_edp - target_edp) / cur_edp;

            // 添加到防振荡滞环
            anti_oscillation_.add(edp_diff);

            // 检查是否应该迁移
            if (anti_oscillation_.should_migrate()) {
                result.target = *mmkp_target;
                result.go = true;
                anti_oscillation_.update(true);
                cool_ = get_cooling_period(false, game);
                return result;
            }
        }
    }

    return result;
}

// 默认参数版本
float MigrationEngineV2::calc_core_edp(int cpu, float target_fps, uint32_t current_freq) const noexcept {
    // 优先使用传入参数，否则使用成员变量
    if (current_freq == 0) {
        current_freq = current_freq_khz_;
    }
    if (current_freq == 0) {
        current_freq = prof_.freqs[cpu];
    }
    
    switch (role) {
        case CoreRole::PRIME:
            base_power = budget_ ? budget_->prime_power_mw : 7000;
            break;
        case CoreRole::BIG:
            base_power = budget_ ? budget_->big_power_mw : 10800;
            break;
        case CoreRole::MID:
            base_power = budget_ ? budget_->little_power_mw : 5500;
            break;
        case CoreRole::LITTLE:
            base_power = budget_ ? budget_->little_power_mw : 3000;
            break;
        default:
            base_power = 5000;
    }
    
    // ========== 频率感知功耗计算 ==========
    // 功耗与频率成正比 (P ∝ f^3)，但需要考虑 DVFS 曲线
    uint32_t max_freq = prof_.freqs[cpu];
    float freq_ratio = static_cast<float>(current_freq) / static_cast<float>(max_freq);
    
    // 使用 DVFS 功耗模型: power ∝ voltage * frequency
    // 动态功耗: P_dynamic = C * V^2 * f (V ∝ f，所以 P ∝ f^3)
    // 静态功耗忽略
    float power_factor = 1.0f;
    if (freq_ratio > 0.0f && freq_ratio < 1.0f) {
        // 简化: 假设电压与频率成线性关系
        power_factor = 0.3f + 0.7f * freq_ratio * freq_ratio * freq_ratio;
    }
    
    uint32_t power = static_cast<uint32_t>(base_power * power_factor);
    
    float util_norm = static_cast<float>(util) / 1024.0f;
    float fps = target_fps * util_norm;
    if (fps <= 0.0f) return 1e9f;
    return static_cast<float>(power) / (fps * fps);
}

float MigrationEngineV2::calc_total_edp() const noexcept {
    float total = 0.0f;

#if defined(__ARM_NEON) && defined(__aarch64__)
    // NEON 向量化：一次处理 4 个核心
    for (int i = 0; i < 8; i += 4) {
        // 加载 4 个核心的 EDP
        float edp_vals[4] = {metrics_[i].edp, metrics_[i + 1].edp,
                             metrics_[i + 2].edp, metrics_[i + 3].edp};
        float32x4_t edp_vec = vld1q_f32(edp_vals);

        // 加载 4 个核心的利用率 (uint32)
        uint32_t util_vals[4] = {metrics_[i].util, metrics_[i + 1].util,
                                 metrics_[i + 2].util, metrics_[i + 3].util};
        uint32x4_t util_u32 = vld1q_u32(util_vals);
        
        // 用 uint32 比较：只有 > 0 的才参与计算
        uint32x4_t zero_u32 = vdupq_n_u32(0);
        uint32x4_t mask_u32 = vcgtq_u32(util_u32, zero_u32);
        
        // 转换为 float 向量
        float32x4_t mask_f32 = vcvtq_f32_u32(mask_u32);
        edp_vec = vmulq_f32(edp_vec, mask_f32);

        // 累加 EDP
        total += vaddvq_f32(edp_vec);
    }
#else
    // 非 ARM 平台：使用普通循环
    for (int i = 0; i < 8; i++) {
        if (metrics_[i].util > 0) total += metrics_[i].edp;
    }
#endif

    return total;
}

bool MigrationEngineV2::check_capacity(uint32_t total_util) const noexcept {
    uint32_t active = prof_.prime_cores + prof_.big_cores + prof_.little_cores;
    return total_util <= active * 1024;
}

std::optional<int> MigrationEngineV2::find_mmkp_target(int cur) const noexcept {
    std::optional<int> best_target;
    float best_edp = 1e9f;

    uint32_t cur_util = loads_[cur].util;
    uint32_t cur_rq = loads_[cur].run_queue;
    float cur_edp = calc_core_edp(cur, 60.0f);

    for (int i = 0; i < 8; i++) {
        // 预取下一个核心的负载数据，隐藏内存延迟
        if (i + 4 < 8) {
            __builtin_prefetch(&loads_[i + 4].util, 0, 3);
        }

        if (i == cur || loads_[i].util > 870) continue;

        uint32_t combined_util = loads_[i].util + cur_util;
        uint32_t combined_rq = loads_[i].run_queue + cur_rq;
        if (combined_util > 870 || combined_rq > 10) continue;

        float target_edp = calc_core_edp(i, 60.0f);
        CoreRole role = prof_.roles[i];

        if (role == CoreRole::LITTLE) target_edp *= 0.8f;
        else if (role == CoreRole::MID) target_edp *= 0.9f;
        else if (role >= CoreRole::BIG) target_edp *= 0.85f;

        // 缓存拓扑亲和惩罚
        float affinity = in_same_cache_domain(cur, i) ? 1.0f : 0.7f;
        target_edp *= affinity;

        if (target_edp < best_edp) {
            best_edp = target_edp;
            best_target = i;
        }
    }

    // 迁移成本校验
    if (best_target.has_value()) {
        float edp_savings = cur_edp - best_edp;
        uint32_t migration_cost = prof_.migration.migration_cost_us;
        if (edp_savings > migration_cost * 1.5f) {
            return best_target;
        }
    }

    return std::nullopt;
}

float MigrationEngineV2::get_edp_cost() const noexcept { return calc_total_edp(); }
bool MigrationEngineV2::is_overutilized() const noexcept { return overutil_ratio_ > 0.3f; }

const char* MigrationEngineV2::core_type_name(int cpu) const noexcept {
    if (cpu < 0 || cpu >= 8) return "Unknown";
    switch (prof_.roles[cpu]) {
        case CoreRole::PRIME: return "Prime";
        case CoreRole::BIG: return "Big";
        case CoreRole::MID: return "Mid";
        case CoreRole::LITTLE: return "Little";
        default: return "Unknown";
    }
}

void MigrationEngineV2::detect_device_generation() noexcept {
    std::string soc = prof_.soc_name;
    
    if (soc.find("8 Elite") != std::string::npos ||
        soc.find("8 Gen 5") != std::string::npos ||
        soc.find("Dimensity 9400") != std::string::npos ||
        soc.find("Dimensity 9300") != std::string::npos) {
        device_gen_ = DeviceGen::Flagship;
        is_all_big_ = true;
        is_legacy_ = false;
    } else if (prof_.is_all_big) {
        device_gen_ = DeviceGen::Flagship;
        is_all_big_ = true;
        is_legacy_ = false;
    } else if (soc.find("870") != std::string::npos ||
               soc.find("888") != std::string::npos ||
               soc.find("8+") != std::string::npos ||
               soc.find("865") != std::string::npos ||
               soc.find("855") != std::string::npos ||
               soc.find("845") != std::string::npos ||
               soc.find("835") != std::string::npos) {
        device_gen_ = DeviceGen::Legacy;
        is_legacy_ = true;
        is_all_big_ = false;
    } else {
        device_gen_ = DeviceGen::Modern;
        is_legacy_ = false;
        is_all_big_ = false;
    }
}

void MigrationEngineV2::configure_all_big_optimization() noexcept {
    if (!is_all_big_) {
        all_big_config_.enabled = false;
        return;
    }
    
    all_big_config_.enabled = true;
    all_big_config_.low_util_thresh = prof_.migration.little_to_mid > 0 ? prof_.migration.little_to_mid : 256;
    all_big_config_.high_util_thresh = prof_.migration.mid_to_big > 0 ? prof_.migration.mid_to_big : 512;
    all_big_config_.migration_cool = prof_.migration.big_cool > 0 ? prof_.migration.big_cool : 4;
}

int MigrationEngineV2::select_thread_placement(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept {
    if (!is_all_big_) return cur;
    
    if (is_game && prof_.prime_cores > 0) {
        for (int i = 0; i < static_cast<int>(prof_.prime_cores); i++) {
            if (loads_[i].util < 256) return i;
        }
    }
    
    for (int i = prof_.prime_cores; i < static_cast<int>(prof_.prime_cores + prof_.big_cores); i++) {
        if (loads_[i].util < 256) return i;
    }
    return cur;
}

void MigrationEngineV2::reset_stats() noexcept {
    for (auto& t : trend_cache_) t = {};
}

} // namespace hp::device