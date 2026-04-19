#include "device/migration_engine.h"
#include "core/logger.h"
#include <algorithm>
#include <cstring>

namespace hp::device {

// 智能迁移引擎 - 动态负载均衡 + 开销保护 + 老旧设备优化

// 老旧设备 (865及以前) 的迁移阈值 - 动态调整版（基于论文优化）
namespace LegacyThresh {
    // 基础阈值 - 向中核倾斜，提高能效
    static constexpr uint32_t LITTLE_TO_MID_UTIL_BASE = 288;   // 小核→中核基础阈值 (28%) [稍微降低，更早迁移到中核]
    static constexpr uint32_t MID_TO_LITTLE_UTIL_BASE = 224;   // 中核→小核基础阈值 (22%) [提高，更晚迁移回小核]
    static constexpr uint32_t MID_TO_BIG_UTIL_BASE = 640;      // 中核→大核基础阈值 (62.5%) [提高，更晚迁移到大核]

    // 动态调整范围
    static constexpr uint32_t UTIL_ADJUST_RANGE = 64;           // 阈值调整范围 (±6.25%)

    // 冷却期
    static constexpr uint32_t LITTLE_COOL_BASE = 8;              // 小核冷却期基础值
    static constexpr uint32_t MID_COOL_BASE = 6;                // 中核冷却期基础值
    static constexpr uint32_t BIG_COOL_BASE = 4;                 // 大核冷却期基础值

    // 负载均衡参数
    static constexpr float LOAD_BALANCE_THRESHOLD = 0.3f;       // 负载差异阈值 (30%)
    static constexpr uint32_t LOAD_BALANCE_MIN_UTIL = 256;       // 负载均衡最小利用率 (25%)

    // 动态负载均衡参数
    static constexpr uint32_t MID_CORE_TARGET_UTIL = 384;       // 中核目标利用率 (37.5%)
    static constexpr uint32_t MID_CORE_UTIL_TOLERANCE = 64;     // 中核利用率容差 (±6.25%)

    // 功率感知调度参数
    static constexpr uint32_t HIGH_POWER_THRESHOLD = 2000;     // 高功耗阈值 (mW)
    static constexpr uint32_t LOW_POWER_THRESHOLD = 1000;      // 低功耗阈值 (mW)

    // 任务分类参数
    static constexpr uint32_t COMPUTE_INTENSIVE_THRESHOLD = 512;  // 计算密集型阈值 (50%)
    static constexpr uint32_t MEMORY_INTENSIVE_THRESHOLD = 256;   // 内存密集型阈值 (25%)
}

// 全大核设备 (8 Elite, 9400等) 的迁移阈值
namespace AllBigThresh {
    static constexpr uint32_t LOW_UTIL = 256;     // 低负载阈值 (25%)
    static constexpr uint32_t HIGH_UTIL = 512;    // 高负载阈值 (50%)
    static constexpr uint32_t RQ_THRESHOLD = 2;   // 运行队列阈值
    static constexpr uint32_t MIGRATION_COOL = 4; // 冷却期 (更短，更灵活)
    
    // 超大核专用阈值
    static constexpr uint32_t PRIME_LOW_UTIL = 384;   // 超大核低负载阈值 (37.5%)
    static constexpr uint32_t PRIME_HIGH_UTIL = 640; // 超大核高负载阈值 (62.5%)
    static constexpr uint32_t PRIME_RQ_THRESHOLD = 3; // 超大核运行队列阈值
    
    // 性能核专用阈值
    static constexpr uint32_t PERF_LOW_UTIL = 192;   // 性能核低负载阈值 (18.75%)
    static constexpr uint32_t PERF_HIGH_UTIL = 448;  // 性能核高负载阈值 (43.75%)
    static constexpr uint32_t PERF_RQ_THRESHOLD = 2; // 性能核运行队列阈值
}

// 现代设备的迁移阈值
namespace ModernThresh {
    static constexpr uint32_t LITTLE_TO_MID_UTIL = 384;   // 小核→中核阈值
    static constexpr uint32_t MID_TO_LITTLE_UTIL = 128;   // 中核→小核阈值
    static constexpr uint32_t MID_TO_BIG_UTIL = 512;      // 中核→大核阈值
    static constexpr uint32_t LITTLE_COOL = 6;
    static constexpr uint32_t MID_COOL = 4;
}

// =============================================================================
// 辅助函数：动态阈值计算
// =============================================================================

// 计算动态阈值（基于温度、电池、场景）
static uint32_t calc_dynamic_threshold(uint32_t base_threshold, uint32_t therm, uint32_t battery, bool is_game) {
    int32_t adjust = 0;

    // 温度调整：温度越高，阈值越高（减少迁移）
    if (therm < 10) {
        adjust += 32;  // 温度低，降低阈值，更积极迁移
    } else if (therm < 20) {
        adjust += 16;
    } else if (therm > 40) {
        adjust -= 32;  // 温度高，提高阈值，减少迁移
    } else if (therm > 30) {
        adjust -= 16;
    }

    // 电池调整：电量低时更保守
    if (battery < 20) {
        adjust += 48;  // 电量低，提高阈值，减少迁移
    } else if (battery < 40) {
        adjust += 24;
    }

    // 场景调整：游戏时更积极
    if (is_game) {
        adjust -= 32;  // 游戏时降低阈值，更积极迁移
    }

    // 应用调整，限制在合理范围内
    int32_t result = static_cast<int32_t>(base_threshold) + adjust;
    result = std::clamp(result, 128, 896);  // 限制在 [12.5%, 87.5%]

    return static_cast<uint32_t>(result);
}

// 计算中核平均利用率
static uint32_t calc_mid_core_avg_util(const std::array<MigrationEngine::CoreLoad, 8>& loads, const std::array<CoreRole, 8>& roles) {
    uint32_t total_util = 0;
    uint32_t mid_count = 0;

    for (int i = 0; i < 8; ++i) {
        if (roles[i] == CoreRole::MID) {
            total_util += loads[i].util;
            mid_count++;
        }
    }

    if (mid_count == 0) return 0;
    return total_util / mid_count;
}

// 动态调整小核→中核的迁移阈值（基于中核利用率）
static uint32_t calc_little_to_mid_threshold(uint32_t base_threshold, uint32_t mid_avg_util) {
    int32_t adjust = 0;

    // 如果中核利用率过低，降低阈值（让更多任务迁移到中核）
    if (mid_avg_util < LegacyThresh::MID_CORE_TARGET_UTIL - LegacyThresh::MID_CORE_UTIL_TOLERANCE) {
        // 中核利用率过低，需要更多任务
        int32_t diff = static_cast<int32_t>(LegacyThresh::MID_CORE_TARGET_UTIL) - static_cast<int32_t>(mid_avg_util);
        adjust = -diff / 2;  // 降低阈值
    }
    // 如果中核利用率过高，提高阈值（减少小核向中核的迁移）
    else if (mid_avg_util > LegacyThresh::MID_CORE_TARGET_UTIL + LegacyThresh::MID_CORE_UTIL_TOLERANCE) {
        // 中核利用率过高，需要减少任务
        int32_t diff = static_cast<int32_t>(mid_avg_util) - static_cast<int32_t>(LegacyThresh::MID_CORE_TARGET_UTIL);
        adjust = diff / 2;  // 提高阈值
    }

    // 应用调整，限制在合理范围内
    int32_t result = static_cast<int32_t>(base_threshold) + adjust;
    result = std::clamp(result, 192, 448);  // 限制在 [18.75%, 43.75%]

    return static_cast<uint32_t>(result);
}

// 功率感知调度：根据功耗情况调整阈值
static uint32_t calc_power_aware_threshold(uint32_t base_threshold, uint32_t power_mw) {
    int32_t adjust = 0;

    // 如果功耗过高，提高阈值（减少迁移，降低功耗）
    if (power_mw > LegacyThresh::HIGH_POWER_THRESHOLD) {
        int32_t diff = static_cast<int32_t>(power_mw) - static_cast<int32_t>(LegacyThresh::HIGH_POWER_THRESHOLD);
        adjust = diff / 100;  // 功耗越高，阈值越高
    }
    // 如果功耗过低，降低阈值（更积极迁移，提高性能）
    else if (power_mw < LegacyThresh::LOW_POWER_THRESHOLD) {
        int32_t diff = static_cast<int32_t>(LegacyThresh::LOW_POWER_THRESHOLD) - static_cast<int32_t>(power_mw);
        adjust = -diff / 100;  // 功耗越低，阈值越低
    }

    // 应用调整，限制在合理范围内
    int32_t result = static_cast<int32_t>(base_threshold) + adjust;
    result = std::clamp(result, 128, 896);  // 限制在 [12.5%, 87.5%]

    return static_cast<uint32_t>(result);
}

// 任务分类：根据任务特征判断任务类型
enum class TaskType {
    COMPUTE_INTENSIVE,  // 计算密集型
    MEMORY_INTENSIVE,   // 内存密集型
    IO_INTENSIVE,       // IO密集型
    UNKNOWN             // 未知
};

static TaskType classify_task(uint32_t util, uint32_t run_queue, uint32_t wakeups) {
    // 计算密集型：高利用率，低运行队列，低唤醒次数
    if (util > LegacyThresh::COMPUTE_INTENSIVE_THRESHOLD && run_queue < 2 && wakeups < 10) {
        return TaskType::COMPUTE_INTENSIVE;
    }
    // 内存密集型：中等利用率，中等运行队列，中等唤醒次数
    else if (util > LegacyThresh::MEMORY_INTENSIVE_THRESHOLD && run_queue >= 2 && wakeups >= 10) {
        return TaskType::MEMORY_INTENSIVE;
    }
    // IO密集型：低利用率，高运行队列，高唤醒次数
    else if (util <= LegacyThresh::MEMORY_INTENSIVE_THRESHOLD && run_queue >= 4 && wakeups >= 20) {
        return TaskType::IO_INTENSIVE;
    }
    // 未知
    else {
        return TaskType::UNKNOWN;
    }
}

// 根据任务类型选择目标核心
static int select_target_by_task_type(TaskType task_type, const std::array<MigrationEngine::CoreLoad, 8>& loads, const std::array<CoreRole, 8>& roles) {
    int best_target = -1;
    uint32_t best_score = 0;

    for (int i = 0; i < 8; ++i) {
        uint32_t score = 0;

        switch (task_type) {
            case TaskType::COMPUTE_INTENSIVE:
                // 计算密集型 → 大核
                if (roles[i] >= CoreRole::BIG) {
                    score = (1024 - loads[i].util) + (8 - loads[i].run_queue) * 64;
                }
                break;
            case TaskType::MEMORY_INTENSIVE:
                // 内存密集型 → 中核
                if (roles[i] == CoreRole::MID) {
                    score = (1024 - loads[i].util) + (8 - loads[i].run_queue) * 64 + 128;  // 中核额外加分
                }
                break;
            case TaskType::IO_INTENSIVE:
                // IO密集型 → 小核
                if (roles[i] == CoreRole::LITTLE) {
                    score = (1024 - loads[i].util) + (8 - loads[i].run_queue) * 64 + 128;  // 小核额外加分
                }
                break;
            case TaskType::UNKNOWN:
                // 未知 → 中核（默认）
                if (roles[i] == CoreRole::MID) {
                    score = (1024 - loads[i].util) + (8 - loads[i].run_queue) * 64;
                }
                break;
        }

        if (score > best_score) {
            best_score = score;
            best_target = i;
        }
    }

    return best_target;
}

// 计算动态冷却期（基于迁移效果）
static uint32_t calc_dynamic_cooling(uint32_t base_cool, uint32_t util, uint32_t target_util) {
    // 如果目标核心负载明显更低，缩短冷却期（迁移效果好）
    if (target_util < util - 128) {
        return std::max(2u, base_cool / 2);
    }
    // 如果目标核心负载相近，延长冷却期（避免抖动）
    else if (target_util > util - 64) {
        return base_cool * 2;
    }
    return base_cool;
}

// 计算整体负载分布（用于负载均衡）
static float calc_load_distribution(const std::array<MigrationEngine::CoreLoad, 8>& loads, const std::array<CoreRole, 8>& roles) {
    uint32_t total_util = 0;
    uint32_t active_cores = 0;

    for (int i = 0; i < 8; ++i) {
        if (loads[i].util > 64) {  // 忽略空闲核心
            total_util += loads[i].util;
            active_cores++;
        }
    }

    if (active_cores == 0) return 0.0f;

    float avg_util = static_cast<float>(total_util) / active_cores;
    float variance = 0.0f;

    for (int i = 0; i < 8; ++i) {
        if (loads[i].util > 64) {
            float diff = static_cast<float>(loads[i].util) - avg_util;
            variance += diff * diff;
        }
    }

    float std_dev = std::sqrt(variance / active_cores);
    return std_dev / (avg_util + 1.0f);  // 返回变异系数
}

// 检查是否需要负载均衡
static bool should_balance_load(const std::array<MigrationEngine::CoreLoad, 8>& loads, const std::array<CoreRole, 8>& roles, int cur_cpu) {
    // 计算当前核心的负载
    uint32_t cur_util = loads[cur_cpu].util;

    // 如果当前核心负载太低，不需要均衡
    if (cur_util < LegacyThresh::LOAD_BALANCE_MIN_UTIL) {
        return false;
    }

    // 计算同级核心的平均负载
    CoreRole cur_role = roles[cur_cpu];
    uint32_t same_role_total = 0;
    uint32_t same_role_count = 0;

    for (int i = 0; i < 8; ++i) {
        if (roles[i] == cur_role && loads[i].util > 64) {
            same_role_total += loads[i].util;
            same_role_count++;
        }
    }

    if (same_role_count == 0) return false;

    float avg_same_role = static_cast<float>(same_role_total) / same_role_count;

    // 如果当前核心负载明显高于同级平均，需要均衡
    return static_cast<float>(cur_util) > avg_same_role * (1.0f + LegacyThresh::LOAD_BALANCE_THRESHOLD);
}

void MigrationEngine::update(int cpu, uint32_t util, uint32_t rq) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    auto& l = loads_[cpu];

    // EMA 更新：3/4 历史 + 1/4 新值，平滑负载波动
    l.util = l.util * 3 / 4 + util / 4;
    l.run_queue = l.run_queue * 3 / 4 + rq / 4;

    // 更新负载趋势
    update_trend(cpu, util);
}

// 重载版本：包含唤醒次数（用于任务分类）
void MigrationEngine::update(int cpu, uint32_t util, uint32_t rq, uint32_t wakeups) noexcept {
    if (cpu < 0 || cpu >= 8) return;
    auto& l = loads_[cpu];

    // EMA 更新：3/4 历史 + 1/4 新值，平滑负载波动
    l.util = l.util * 3 / 4 + util / 4;
    l.run_queue = l.run_queue * 3 / 4 + rq / 4;
    l.wakeups = l.wakeups * 3 / 4 + wakeups / 4;

    // 更新负载趋势
    update_trend(cpu, util);
}

// 更新负载趋势
void MigrationEngine::update_trend(int cpu, uint32_t util) noexcept {
    if (cpu < 0 || cpu >= 8) return;

    auto& trend = trend_cache_[cpu];
    auto now = std::chrono::steady_clock::now();

    // 计算时间差（毫秒）
    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - trend.last_update).count();

    if (time_diff > 0) {
        // 计算负载变化速度 (util/ms)
        float util_diff = static_cast<float>(util) - static_cast<float>(trend.prev_util);
        trend.velocity = util_diff / time_diff;

        // EMA 平滑速度
        trend.velocity = trend.velocity * 0.7f + trend.velocity * 0.3f;
    }

    trend.prev_util = util;
    trend.last_update = now;
}

// 获取负载趋势
float MigrationEngine::get_util_trend(int cpu) const noexcept {
    if (cpu < 0 || cpu >= 8) return 0.0f;
    return trend_cache_[cpu].velocity;
}

MigResult MigrationEngine::decide(int cur, uint32_t therm, bool is_game) noexcept {
    MigResult r;
    r.target = cur;
    r.go = false;
    r.thermal = false;
    
    // ================== 1. 温控紧急降级 (最高优先级) ==================
    if (therm < 5) {
        r.thermal = true;
        r.go = true;
        // 迁移到低功耗核心
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] <= CoreRole::MID) {
                r.target = i;
                break;
            }
        }
        if (r.target == cur) {
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] < CoreRole::PRIME) {
                    r.target = i;
                    break;
                }
            }
        }
        cool_ = LegacyThresh::LITTLE_COOL_BASE;
        return r;
    }
    
    // ================== 2. 冷却期检查 ==================
    if (cool_ > 0) {
        cool_--;
        return r;
    }
    
    // ================== 3. 获取当前状态 ==================
    float util_norm = static_cast<float>(loads_[cur].util) / 1024.f;
    uint32_t run_queue = loads_[cur].run_queue;
    uint32_t util = loads_[cur].util;
    CoreRole cur_role = prof_.roles[cur];
    
    // 计算程序开销阈值
    static constexpr uint32_t MIGRATION_COST_US = 500;
    
    // ================== 4. 游戏模式: 直接绑定大核 ==================
    if (is_game) {
        if (cur_role < CoreRole::BIG) {
            for (int i = 7; i >= 0; --i) {
                if (prof_.roles[i] >= CoreRole::BIG && loads_[i].run_queue < 4) {
                    r.target = i;
                    r.go = true;
                    break;
                }
            }
        }
        cool_ = is_game ? 2 : 6;
        return r;
    }
    
        // ================== 5. 老旧设备优化 (865及以前) ==================
        // 老旧设备没有超大核，中核就是高性能核
        // 策略: 小核↔中核迁移，避免频繁唤醒大核
        if (is_legacy_) {
            // 获取当前核心的负载趋势
            float util_trend = get_util_trend(cur);

            // 计算中核平均利用率
            uint32_t mid_avg_util = calc_mid_core_avg_util(loads_, prof_.roles);

            // 任务分类
            TaskType task_type = classify_task(util, run_queue, loads_[cur].wakeups);

            // 计算动态阈值
            uint32_t battery_level = loads_[cur].util > 0 ? 100 : 50;  // 简化处理，实际应从 LoadFeature 获取
            uint32_t power_mw = 1500;  // 简化处理，实际应从 LoadFeature 获取

            uint32_t little_to_mid_thresh = calc_little_to_mid_threshold(
                LegacyThresh::LITTLE_TO_MID_UTIL_BASE, mid_avg_util);
            little_to_mid_thresh = calc_power_aware_threshold(little_to_mid_thresh, power_mw);

            uint32_t mid_to_little_thresh = calc_dynamic_threshold(
                LegacyThresh::MID_TO_LITTLE_UTIL_BASE, therm, battery_level, is_game);
            mid_to_little_thresh = calc_power_aware_threshold(mid_to_little_thresh, power_mw);

            uint32_t mid_to_big_thresh = calc_dynamic_threshold(
                LegacyThresh::MID_TO_BIG_UTIL_BASE, therm, battery_level, is_game);
            mid_to_big_thresh = calc_power_aware_threshold(mid_to_big_thresh, power_mw);

            // 负载趋势调整：如果负载在快速上升，提前迁移
            if (util_trend > 0.5f) {  // 负载快速上升
                little_to_mid_thresh -= 32;  // 降低阈值，提前迁移
                mid_to_big_thresh -= 64;
            } else if (util_trend < -0.5f) {  // 负载快速下降
                mid_to_little_thresh += 64;  // 提高阈值，延迟下沉
            }

            // 检查是否需要负载均衡
            bool need_balance = should_balance_load(loads_, prof_.roles, cur);

            // --- 老旧设备: 小核 → 中核 (轻负载时) ---
            if (cur_role == CoreRole::LITTLE && util > little_to_mid_thresh) {
                // 根据任务类型选择目标核心
                int best_mid = select_target_by_task_type(task_type, loads_, prof_.roles);

                // 如果任务分类没有找到合适的目标，使用原来的逻辑
                if (best_mid < 0 || prof_.roles[best_mid] != CoreRole::MID) {
                    uint32_t best_score = 0;
                    for (int i = 0; i < 8; ++i) {
                        if (prof_.roles[i] == CoreRole::MID) {
                            // 综合评分：负载越低、运行队列越短、负载趋势越稳定，分数越高
                            float target_trend = get_util_trend(i);
                            uint32_t trend_score = static_cast<uint32_t>(std::max(0.0f, 1.0f - std::abs(target_trend)) * 128);
                            uint32_t score = (1024 - loads_[i].util) + (8 - loads_[i].run_queue) * 64 + trend_score;

                            // 考虑负载均衡需求
                            if (need_balance) {
                                score += 128;  // 优先选择负载均衡
                            }

                            // 865 优化：优先选择中核，提高中核的权重
                            score += 64;  // 中核额外加分

                            if (score > best_score) {
                                best_score = score;
                                best_mid = i;
                            }
                        }
                    }
                }

                if (best_mid >= 0) {
                    uint32_t save = estimate_power_savings(cur, best_mid, util);
                    // 动态冷却期
                    uint32_t dynamic_cool = calc_dynamic_cooling(
                        LegacyThresh::LITTLE_COOL_BASE, util, loads_[best_mid].util);

                    // 865 优化：降低迁移成本阈值，更积极地迁移到中核
                    if (save > MIGRATION_COST_US / 2 || util > little_to_mid_thresh + 32) {
                        r.target = best_mid;
                        r.go = true;
                        cool_ = dynamic_cool;
                        LOGD("Mig[Legacy]: LITTLE->MID CPU%d->%d util=%u mid_avg=%u thresh=%u task_type=%d trend=%.2f cool=%u",
                             cur, best_mid, util, mid_avg_util, little_to_mid_thresh, static_cast<int>(task_type), util_trend, dynamic_cool);
                        return r;
                    }
                }
            }

        // --- 老旧设备: 中核 → 小核 (负载降低时) ---
        if (cur_role == CoreRole::MID && util < mid_to_little_thresh && run_queue < 2) {
            // 寻找最优目标小核
            int best_little = -1;
            uint32_t best_score = 0;

            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::LITTLE) {
                    // 综合评分：负载越低、运行队列越短、负载趋势越稳定，分数越高
                    float target_trend = get_util_trend(i);
                    uint32_t trend_score = static_cast<uint32_t>(std::max(0.0f, 1.0f - std::abs(target_trend)) * 128);
                    uint32_t score = (1024 - loads_[i].util) + (8 - loads_[i].run_queue) * 64 + trend_score;

                    if (score > best_score) {
                        best_score = score;
                        best_little = i;
                    }
                }
            }

            if (best_little >= 0) {
                // 动态冷却期
                uint32_t dynamic_cool = calc_dynamic_cooling(
                    LegacyThresh::MID_COOL_BASE, util, loads_[best_little].util);

                r.target = best_little;
                r.go = true;
                cool_ = dynamic_cool;
                LOGD("Mig[Legacy]: MID->LITTLE CPU%d->%d util=%u trend=%.2f cool=%u",
                     cur, best_little, util, util_trend, dynamic_cool);
                return r;
            }
        }

        // --- 老旧设备: 中核 → 大核 (高负载时) ---
        if (cur_role == CoreRole::MID && util > mid_to_big_thresh) {
            // 寻找最优目标大核
            int best_big = -1;
            uint32_t best_score = 0;

            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] >= CoreRole::BIG) {
                    // 综合评分：负载越低、运行队列越短、负载趋势越稳定，分数越高
                    float target_trend = get_util_trend(i);
                    uint32_t trend_score = static_cast<uint32_t>(std::max(0.0f, 1.0f - std::abs(target_trend)) * 128);
                    uint32_t score = (1024 - loads_[i].util) + (8 - loads_[i].run_queue) * 64 + trend_score;

                    if (score > best_score) {
                        best_score = score;
                        best_big = i;
                    }
                }
            }

            if (best_big >= 0) {
                // 动态冷却期
                uint32_t dynamic_cool = calc_dynamic_cooling(
                    LegacyThresh::BIG_COOL_BASE, util, loads_[best_big].util);

                // 865 优化：提高迁移成本阈值，更保守地迁移到大核
                uint32_t save = estimate_power_savings(cur, best_big, util);
                if (save > MIGRATION_COST_US * 2 || util > mid_to_big_thresh + 64) {
                    r.target = best_big;
                    r.go = true;
                    cool_ = dynamic_cool;
                    LOGD("Mig[Legacy]: MID->BIG CPU%d->%d util=%u trend=%.2f cool=%u",
                         cur, best_big, util, util_trend, dynamic_cool);
                    return r;
                }
            }
        }

        // --- 老旧设备: 负载均衡 (同级核心间) ---
        if (need_balance && util > LegacyThresh::LOAD_BALANCE_MIN_UTIL) {
            // 寻找同级负载最低且趋势稳定的核心
            int best_same_role = -1;
            uint32_t min_util = util;
            float best_trend = 0.0f;

            for (int i = 0; i < 8; ++i) {
                if (i != cur && prof_.roles[i] == cur_role && loads_[i].util < min_util) {
                    float target_trend = get_util_trend(i);
                    // 优先选择负载低且趋势稳定的核心
                    if (loads_[i].util < min_util - 64 ||
                        std::abs(target_trend) < std::abs(best_trend)) {
                        min_util = loads_[i].util;
                        best_trend = target_trend;
                        best_same_role = i;
                    }
                }
            }

            if (best_same_role >= 0 && min_util < util - 128) {
                // 动态冷却期
                uint32_t dynamic_cool = calc_dynamic_cooling(
                    LegacyThresh::MID_COOL_BASE, util, min_util);

                r.target = best_same_role;
                r.go = true;
                cool_ = dynamic_cool;
                LOGD("Mig[Legacy]: Balance CPU%d->%d util=%u->%u trend=%.2f cool=%u",
                     cur, best_same_role, util, min_util, best_trend, dynamic_cool);
                return r;
            }
        }
    }
    
    // ================== 6. 全大核设备优化 (8 Elite, 9400等) ==================
    // 全大核没有小核，所有核心都是高性能核心
    // 策略: 更激进的负载均衡，充分利用所有核心
    if (is_all_big_) {
        // 使用智能线程放置
        int placement = select_thread_placement(cur, util, run_queue, is_game);
        if (placement != cur) {
            r.target = placement;
            r.go = true;
            cool_ = all_big_config_.migration_cool;
            LOGD("Mig[AllBig]: Placement CPU%d->%d util=%u rq=%u", cur, placement, util, run_queue);
            return r;
        }
        
        // 使用全大核专用迁移策略
        auto target = find_all_big_target(cur, util, run_queue, is_game);
        if (target.has_value()) {
            r.target = target.value();
            r.go = true;
            cool_ = all_big_config_.migration_cool;
            LOGD("Mig[AllBig]: Target CPU%d->%d util=%u rq=%u", cur, r.target, util, run_queue);
            return r;
        }
        
        // 轻负载: 允许任何核心处理
        if (util < all_big_config_.low_util_thresh && run_queue < 2) {
            // 不迁移，让调度器自由选择
            return r;
        }
        
        // 高负载: 负载均衡
        if (util > all_big_config_.high_util_thresh || run_queue > 2) {
            // 找负载最轻的核心
            uint32_t min_load = util;
            int target_cpu = cur;
            for (int i = 0; i < 8; ++i) {
                if (i == cur) continue;
                uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
                if (total_load < min_load) {
                    min_load = total_load;
                    target_cpu = i;
                }
            }
            if (target_cpu != cur) {
                r.target = target_cpu;
                r.go = true;
                cool_ = all_big_config_.migration_cool;
                LOGD("Mig[AllBig]: Balance CPU%d->%d util=%u", cur, target_cpu, util);
                return r;
            }
        }
    }
    
    // ================== 6. 现代设备: 轻负载保护 ==================
    // 轻负载判断: util < 128 (12.5%) 且 run_queue < 2
    if (util < 128 && run_queue < 2 && cur_role <= CoreRole::MID) {
        return r;
    }
    
    // ================== 7. 现代设备: 大核下沉 ==================
    // 如果当前在大核且负载不高，检查是否应该下沉到中核
    if (cur_role >= CoreRole::BIG && util < 384) {
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] < CoreRole::BIG && 
                loads_[i].util < util &&
                loads_[i].run_queue < run_queue) {
                uint32_t estimated_save = estimate_power_savings(cur, i, util);
                if (estimated_save > MIGRATION_COST_US) {
                    r.target = i;
                    r.go = true;
                    cool_ = 6;
                    return r;
                }
            }
        }
    }
    
    // ================== 8. 现代设备: 小核上浮 ==================
    // 小核负载过高，上浮到中核
    if (cur_role <= CoreRole::MID && util > 384) {
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] >= CoreRole::BIG && 
                loads_[i].util < util &&
                loads_[i].run_queue < run_queue + 2) {
                r.target = i;
                r.go = true;
                cool_ = 4;
                return r;
            }
        }
    }
    
    // ================== 9. 高负载: 负载均衡 ==================
    if (run_queue > 3) {
        for (int i = 0; i < 8; ++i) {
            if (i == cur) continue;
            // 优先同级核心
            if (prof_.roles[i] == cur_role && 
                loads_[i].run_queue < run_queue) {
                r.target = i;
                r.go = true;
                break;
            }
        }
    }
    
    // ================== 10. 设置冷却期 ==================
    if (r.go) {
        cool_ = is_legacy_ ? LegacyThresh::LITTLE_COOL_BASE : 6;
        static int log_cnt = 0;
        if (++log_cnt % 30 == 0) {
            LOGD("Mig: CPU%d→%d | Util=%u | RQ=%u | Legacy=%s",
                 cur, r.target, util, run_queue, is_legacy_ ? "true" : "false");
        }
    }
    
    return r;
}

// 估算迁移节省的功耗 (微秒当量) - 优化版
uint32_t MigrationEngine::estimate_power_savings(int from_cpu, int to_cpu, uint32_t util) noexcept {
    // 基于核心类型计算
    auto from_role = prof_.roles[from_cpu];
    auto to_role = prof_.roles[to_cpu];

    // 如果从省电核心迁移到费电核心，检查是否有性能收益
    if (from_role <= CoreRole::MID && to_role >= CoreRole::BIG) {
        // 这种情况更耗电，但可能有性能收益
        // 只有在高负载时才考虑
        if (util > 512) {
            // 高负载时，大核可能更高效（性能提升 > 功耗增加）
            return 200;  // 小的正收益
        }
        return 0;  // 低负载时不迁移
    }

    // 大核→小核: 潜在省电
    if (from_role >= CoreRole::BIG && to_role <= CoreRole::MID) {
        // 根据负载预估节省
        // 重负载在大核更高效，轻负载在小核更省电
        if (util < 256) {
            return 1500;  // 轻负载迁移预估节省更多
        } else if (util < 384) {
            return 1000;  // 中等负载
        } else if (util < 512) {
            return 500;   // 较高负载
        } else {
            return 200;   // 高负载时省电较少
        }
    }

    // 小核→中核: 性能提升
    if (from_role == CoreRole::LITTLE && to_role == CoreRole::MID) {
        // 中核性能更好，但功耗略高
        // 在中等负载时收益最大
        if (util > 320 && util < 512) {
            return 800;  // 中等负载时性能提升明显
        } else if (util > 512) {
            return 1200;  // 高负载时性能提升更大
        }
        return 400;  // 低负载时收益较小
    }

    // 中核→小核: 省电
    if (from_role == CoreRole::MID && to_role == CoreRole::LITTLE) {
        // 小核更省电，但性能较差
        // 在低负载时收益最大
        if (util < 192) {
            return 1200;  // 低负载时省电明显
        } else if (util < 256) {
            return 800;   // 中等负载
        }
        return 400;  // 较高负载时收益较小
    }

    // 同级核心迁移: 负载均衡
    if (from_role == to_role) {
        // 负载均衡可以减少热点，提升整体性能
        // 收益取决于负载差异
        uint32_t load_diff = std::abs(static_cast<int>(loads_[from_cpu].util) - static_cast<int>(loads_[to_cpu].util));
        if (load_diff > 256) {
            return 600;  // 负载差异大时收益明显
        } else if (load_diff > 128) {
            return 300;  // 负载差异中等
        }
        return 100;  // 负载差异小
    }

    return 0;
}

void MigrationEngine::reset_stats() noexcept {
    for (auto& t : trend_cache_) {
        t = {};
    }
}

// =============================================================================
// 设备代数识别
// =============================================================================

// 检测设备代数 (865及以前为老旧设备)
void MigrationEngine::detect_device_generation() noexcept {
    std::string soc = prof_.soc_name;
    
    // 全大核设备识别 (没有小核)
    // 1. 通过 SoC 名称识别
    if (soc.find("8 Elite") != std::string::npos ||
        soc.find("8 Gen 5") != std::string::npos ||
        soc.find("SM8850") != std::string::npos ||
        soc.find("SM8750") != std::string::npos ||
        soc.find("Dimensity 9400") != std::string::npos ||
        soc.find("MT6991") != std::string::npos ||
        soc.find("Dimensity 9300") != std::string::npos ||
        soc.find("MT6985") != std::string::npos) {
        device_gen_ = DeviceGen::Flagship;
        is_all_big_ = true;
        is_legacy_ = false;
    }
    // 2. 通过硬件配置识别
    else if (prof_.is_all_big) {
        device_gen_ = DeviceGen::Flagship;
        is_all_big_ = true;
        is_legacy_ = false;
    }
    // 3. 通过核心数量识别 (8个核心都是大核或性能核)
    else {
        uint8_t big_count = 0;
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] >= CoreRole::BIG) {
                big_count++;
            }
        }
        if (big_count >= 6) {  // 6个或以上大核
            device_gen_ = DeviceGen::Flagship;
            is_all_big_ = true;
            is_legacy_ = false;
        }
        // 老旧设备识别 (865及以前 + 8Gen2/3 + 888/8+)
        else if (soc.find("888") != std::string::npos ||
            soc.find("8+") != std::string::npos ||
            soc.find("8 Gen 1") != std::string::npos ||
            soc.find("SM8450") != std::string::npos ||
            soc.find("865") != std::string::npos ||
            soc.find("855") != std::string::npos ||
            soc.find("845") != std::string::npos ||
            soc.find("835") != std::string::npos ||
            soc.find("821") != std::string::npos ||
            soc.find("820") != std::string::npos ||
            soc.find("810") != std::string::npos ||
            soc.find("730") != std::string::npos ||
            soc.find("720") != std::string::npos ||
            soc.find("Dimensity 7") != std::string::npos ||
            soc.find("Helio") != std::string::npos ||
            soc == "Unknown") {
            device_gen_ = DeviceGen::Legacy;
            is_legacy_ = true;
            is_all_big_ = false;
        }
        // 8Gen2/3 优化 (应用老旧设备的优化策略)
        else if (soc.find("SM8550") != std::string::npos ||  // 8 Gen 2
            soc.find("SM8650") != std::string::npos ||      // 8 Gen 3
            soc.find("8 Gen 2") != std::string::npos ||
            soc.find("8 Gen 3") != std::string::npos) {
            device_gen_ = DeviceGen::Modern;
            is_legacy_ = true;  // 启用老旧设备的优化策略
            is_all_big_ = false;
        } else {
            device_gen_ = DeviceGen::Modern;
            is_legacy_ = false;
            is_all_big_ = false;
        }
    }
    
    LOGI("Migration: Legacy=%s, AllBig=%s", 
         is_legacy_ ? "true" : "false", 
         is_all_big_ ? "true" : "false");
    LOGI("Migration: DeviceGen=%d (Legacy=%s)", 
         static_cast<int>(device_gen_), 
         is_legacy_ ? "true" : "false");
}

// =============================================================================
// 全大核设备优化
// =============================================================================

// 配置全大核设备优化
void MigrationEngine::configure_all_big_optimization() noexcept {
    if (!is_all_big_) {
        all_big_config_.enabled = false;
        return;
    }
    
    all_big_config_.enabled = true;
    
    // 统计核心数量
    uint8_t prime_count = 0;
    uint8_t perf_count = 0;
    uint32_t max_freq = 0;
    uint32_t min_freq = UINT32_MAX;
    
    for (int i = 0; i < 8; ++i) {
        if (prof_.roles[i] == CoreRole::PRIME) {
            prime_count++;
        } else if (prof_.roles[i] >= CoreRole::BIG) {
            perf_count++;
        }
    }
    
    all_big_config_.prime_count = prime_count;
    all_big_config_.perf_count = perf_count;
    all_big_config_.has_prime_cores = (prime_count > 0);
    
    // 计算频率比
    if (min_freq != UINT32_MAX && max_freq > 0) {
        all_big_config_.freq_ratio = static_cast<float>(max_freq) / min_freq;
    }
    
    // 根据核心配置调整阈值
    if (all_big_config_.has_prime_cores) {
        // 有超大核的设备 (如 8 Elite, 9400)
        all_big_config_.low_util_thresh = AllBigThresh::PRIME_LOW_UTIL;
        all_big_config_.high_util_thresh = AllBigThresh::PRIME_HIGH_UTIL;
        all_big_config_.migration_cool = 3;  // 更短的冷却期
    } else {
        // 没有超大核的设备 (如 9300)
        all_big_config_.low_util_thresh = AllBigThresh::PERF_LOW_UTIL;
        all_big_config_.high_util_thresh = AllBigThresh::PERF_HIGH_UTIL;
        all_big_config_.migration_cool = 4;
    }
    
    LOGI("AllBig Config: Prime=%u Perf=%u FreqRatio=%.2f LowThresh=%u HighThresh=%u Cool=%u",
         all_big_config_.prime_count, all_big_config_.perf_count,
         all_big_config_.freq_ratio, all_big_config_.low_util_thresh,
         all_big_config_.high_util_thresh, all_big_config_.migration_cool);
}

// 智能线程放置 (全大核设备)
int MigrationEngine::select_thread_placement(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept {
    if (!all_big_config_.enabled) return cur;
    
    // 游戏模式: 优先使用超大核
    if (is_game && all_big_config_.has_prime_cores) {
        // 寻找负载最低的超大核
        int best_prime = -1;
        uint32_t min_load = UINT32_MAX;
        
        for (int i = 0; i < 8; ++i) {
            if (prof_.roles[i] == CoreRole::PRIME) {
                uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
                if (total_load < min_load) {
                    min_load = total_load;
                    best_prime = i;
                }
            }
        }
        
        if (best_prime >= 0) {
            return best_prime;
        }
    }
    
    // 高负载: 寻找负载最低的核心
    if (util > all_big_config_.high_util_thresh || rq > 2) {
        int best_cpu = -1;
        uint32_t min_load = UINT32_MAX;
        
        for (int i = 0; i < 8; ++i) {
            uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
            if (total_load < min_load) {
                min_load = total_load;
                best_cpu = i;
            }
        }
        
        if (best_cpu >= 0 && min_load < (util + rq * 128)) {
            return best_cpu;
        }
    }
    
    // 中等负载: 根据任务类型选择核心
    if (util > all_big_config_.low_util_thresh) {
        // 如果当前不在超大核，考虑迁移到超大核
        if (all_big_config_.has_prime_cores && prof_.roles[cur] != CoreRole::PRIME) {
            // 寻找负载最低的超大核
            int best_prime = -1;
            uint32_t min_load = UINT32_MAX;
            
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::PRIME) {
                    uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
                    if (total_load < min_load) {
                        min_load = total_load;
                        best_prime = i;
                    }
                }
            }
            
            if (best_prime >= 0 && min_load < (util + rq * 128) * 0.8f) {
                return best_prime;
            }
        }
    }
    
    // 低负载: 保持当前核心
    return cur;
}

// 全大核设备核间迁移
std::optional<int> MigrationEngine::find_all_big_target(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept {
    if (!all_big_config_.enabled) return std::nullopt;
    
    // 游戏模式: 优先使用超大核
    if (is_game && all_big_config_.has_prime_cores) {
        if (prof_.roles[cur] != CoreRole::PRIME) {
            // 寻找负载最低的超大核
            int best_prime = -1;
            uint32_t min_load = UINT32_MAX;
            
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::PRIME) {
                    uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
                    if (total_load < min_load) {
                        min_load = total_load;
                        best_prime = i;
                    }
                }
            }
            
            if (best_prime >= 0 && min_load < (util + rq * 128) * 0.7f) {
                return best_prime;
            }
        }
    }
    
    // 高负载: 负载均衡
    if (util > all_big_config_.high_util_thresh || rq > 2) {
        int best_cpu = -1;
        uint32_t min_load = UINT32_MAX;
        
        for (int i = 0; i < 8; ++i) {
            if (i == cur) continue;
            uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
            if (total_load < min_load) {
                min_load = total_load;
                best_cpu = i;
            }
        }
        
        if (best_cpu >= 0 && min_load < (util + rq * 128) * 0.8f) {
            return best_cpu;
        }
    }
    
    // 中等负载: 考虑核心类型
    if (util > all_big_config_.low_util_thresh) {
        // 如果当前在性能核，考虑迁移到超大核
        if (all_big_config_.has_prime_cores && prof_.roles[cur] >= CoreRole::BIG && prof_.roles[cur] != CoreRole::PRIME) {
            int best_prime = -1;
            uint32_t min_load = UINT32_MAX;
            
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] == CoreRole::PRIME) {
                    uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
                    if (total_load < min_load) {
                        min_load = total_load;
                        best_prime = i;
                    }
                }
            }
            
            if (best_prime >= 0 && min_load < (util + rq * 128) * 0.6f) {
                return best_prime;
            }
        }
        
        // 如果当前在超大核，考虑迁移到性能核
        if (prof_.roles[cur] == CoreRole::PRIME && util < all_big_config_.high_util_thresh) {
            int best_perf = -1;
            uint32_t min_load = UINT32_MAX;
            
            for (int i = 0; i < 8; ++i) {
                if (prof_.roles[i] >= CoreRole::BIG && prof_.roles[i] != CoreRole::PRIME) {
                    uint32_t total_load = loads_[i].util + loads_[i].run_queue * 128;
                    if (total_load < min_load) {
                        min_load = total_load;
                        best_perf = i;
                    }
                }
            }
            
            if (best_perf >= 0 && min_load < (util + rq * 128) * 0.5f) {
                return best_perf;
            }
        }
    }
    
    // 低负载: 不迁移
    return std::nullopt;
}

} // namespace hp::device