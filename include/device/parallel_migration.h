#pragma once
/**
 * @file parallel_migration.h
 * @brief 并行化迁移引擎
 *
 * 设计原则：
 * 1. 低开销 - 最小化并行化开销
 * 2. 细粒度 - 支持小任务的并行化
 * 3. 负载均衡 - 均匀分配计算任务
 * 4. 大小核感知 - 将计算任务分配到合适的核心
 */

#include "device/migration_engine_v2.h"
#include "core/parallel_compute.h"
#include "core/load_aware_pool.h"
#include "core/task_decomposer.h"
#include <array>
#include <atomic>
#include <chrono>

namespace hp::device {

// =============================================================================
// 并行化迁移引擎
// =============================================================================

class ParallelMigrationEngine {
private:
    MigrationEngineV2 base_engine_;
    parallel::ParallelLoadCompute load_compute_;

    // 并行计算状态
    struct ParallelState {
        std::array<float, 8> util_trends_{};
        std::array<uint32_t, 8> migration_scores_{};
        std::atomic<bool> computing{false};
        std::atomic<uint64_t> last_compute_time{0};
    } parallel_state_;

    // 负载感知配置
    struct LoadConfig {
        float low_threshold{0.3f};      // 低负载阈值
        float high_threshold{0.7f};     // 高负载阈值
        uint32_t cool_down{4};          // 冷却期
        bool enable_parallel{true};     // 是否启用并行
    } load_config_;

public:
    ParallelMigrationEngine() = default;

    // 初始化
    void init(const HardwareProfile& profile) {
        base_engine_.init(profile);
    }

    // 并行更新负载
    void update_parallel(int cpu, uint32_t util, uint32_t rq) noexcept {
        if (!load_config_.enable_parallel) {
            base_engine_.update(cpu, util, rq);
            return;
        }

        // 更新基础引擎
        base_engine_.update(cpu, util, rq);

        // 并行计算趋势
        if (!parallel_state_.computing.load()) {
            parallel_state_.computing.store(true);

            auto& decomposer = parallel::global_task_decomposer();
            decomposer.parallel_for(0, 8, [this, cpu, util](size_t i) {
                if (i == static_cast<size_t>(cpu)) {
                    // 计算当前核心的趋势
                    auto now = std::chrono::steady_clock::now();
                    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - std::chrono::steady_clock::time_point{}).count();

                    if (time_diff > 0) {
                        float util_diff = static_cast<float>(util) -
                                        static_cast<float>(base_engine_.get_load(i).util);
                        parallel_state_.util_trends_[i] = util_diff / time_diff;
                    }
                }
            });

            parallel_state_.computing.store(false);
            parallel_state_.last_compute_time.store(
                std::chrono::steady_clock::now().time_since_epoch().count());
        }
    }

    // 并行迁移决策
    MigResult decide_parallel(int cur, uint32_t therm, bool is_game) noexcept {
        if (!load_config_.enable_parallel) {
            return base_engine_.decide(cur, therm, is_game);
        }

        // 并行计算所有核心的迁移评分
        if (!parallel_state_.computing.load()) {
            parallel_state_.computing.store(true);

            auto& decomposer = parallel::global_task_decomposer();
            const auto& loads = base_engine_.get_all_loads();
            const auto& roles = base_engine_.get_all_roles();

            decomposer.parallel_for(0, 8, [this, &loads, &roles, cur](size_t i) {
                if (i == static_cast<size_t>(cur)) {
                    parallel_state_.migration_scores_[i] = 0;
                    return;
                }

                // 计算迁移评分
                uint32_t load_score = 1024 - loads[i].util;
                uint32_t rq_score = (8 - loads[i].run_queue) * 64;
                uint32_t trend_score = static_cast<uint32_t>(
                    std::max(0.0f, 1.0f - std::abs(parallel_state_.util_trends_[i])) * 128
                );

                // 考虑核心类型
                CoreRole cur_role = roles[cur];
                CoreRole target_role = roles[i];

                // 优先迁移到更合适的核心
                if (cur_role == CoreRole::LITTLE && target_role == CoreRole::MID) {
                    // 小核→中核，加分
                    load_score += 128;
                } else if (cur_role == CoreRole::MID && target_role == CoreRole::LITTLE) {
                    // 中核→小核，加分
                    load_score += 128;
                } else if (cur_role == CoreRole::MID && target_role >= CoreRole::BIG) {
                    // 中核→大核，加分
                    load_score += 256;
                } else if (cur_role >= CoreRole::BIG && target_role == CoreRole::MID) {
                    // 大核→中核，加分
                    load_score += 256;
                }

                parallel_state_.migration_scores_[i] = load_score + rq_score + trend_score;
            });

            parallel_state_.computing.store(false);
        }

        // 查找最优目标
        int best_target = cur;
        uint32_t best_score = parallel_state_.migration_scores_[cur];

        for (int i = 0; i < 8; ++i) {
            if (i != cur && parallel_state_.migration_scores_[i] > best_score) {
                best_score = parallel_state_.migration_scores_[i];
                best_target = i;
            }
        }

        // 构建结果
        MigResult result;
        result.target = best_target;
        result.go = (best_target != cur);
        result.thermal = (therm < 5);

        return result;
    }

    // 并行负载均衡
    void balance_load_parallel() noexcept {
        if (!load_config_.enable_parallel) {
            return;
        }

        // 并行计算负载分布
        const auto& loads = base_engine_.get_all_loads();
        float load_dist = parallel::ParallelLoadCompute::compute_load_distribution(
            {loads[0].util, loads[1].util, loads[2].util, loads[3].util,
             loads[4].util, loads[5].util, loads[6].util, loads[7].util}
        );

        // 如果负载分布不均匀，触发负载均衡
        if (load_dist > load_config_.high_threshold) {
            // 并行查找需要迁移的核心
            auto& decomposer = parallel::global_task_decomposer();
            std::array<int, 8> migration_targets{-1, -1, -1, -1, -1, -1, -1, -1};

            decomposer.parallel_for(0, 8, [&loads, &migration_targets](size_t i) {
                // 查找负载最低的同级核心
                uint32_t min_util = loads[i].util;
                int target = -1;

                for (int j = 0; j < 8; ++j) {
                    if (j != static_cast<int>(i) && loads[j].util < min_util) {
                        min_util = loads[j].util;
                        target = j;
                    }
                }

                if (target >= 0 && min_util < loads[i].util - 128) {
                    migration_targets[i] = target;
                }
            });

            // 执行迁移（这里只是示例，实际需要调用系统API）
            for (int i = 0; i < 8; ++i) {
                if (migration_targets[i] >= 0) {
                    // TODO: 执行实际的线程迁移
                    // sched_setaffinity(...)
                }
            }
        }
    }

    // 并行估算功耗节省
    uint32_t estimate_power_savings_parallel(int from_cpu, int to_cpu, uint32_t util) noexcept {
        if (!load_config_.enable_parallel) {
            return base_engine_.estimate_power_savings(from_cpu, to_cpu, util);
        }

        // 并行计算多个维度的功耗节省
        auto& decomposer = parallel::global_task_decomposer();

        // 维度1: 核心类型差异
        uint32_t type_saving = decomposer.parallel_reduce(
            0, 1, 0u,
            [this, from_cpu, to_cpu](uint32_t acc, size_t) {
                auto from_role = base_engine_.get_role(from_cpu);
                auto to_role = base_engine_.get_role(to_cpu);

                if (from_role >= CoreRole::BIG && to_role <= CoreRole::MID) {
                    return acc + 1000u;  // 大核→小核，省电
                } else if (from_role == CoreRole::LITTLE && to_role == CoreRole::MID) {
                    return acc + 400u;   // 小核→中核，性能提升
                }
                return acc;
            },
            [](uint32_t a, uint32_t b) { return a + b; }
        );

        // 维度2: 负载差异
        uint32_t load_saving = decomposer.parallel_reduce(
            0, 1, 0u,
            [this, from_cpu, to_cpu, util](uint32_t acc, size_t) {
                uint32_t from_util = base_engine_.get_load(from_cpu).util;
                uint32_t to_util = base_engine_.get_load(to_cpu).util;

                if (to_util < from_util - 128) {
                    return acc + 500u;  // 目标核心负载更低
                }
                return acc;
            },
            [](uint32_t a, uint32_t b) { return a + b; }
        );

        // 维度3: 趋势差异
        uint32_t trend_saving = decomposer.parallel_reduce(
            0, 1, 0u,
            [this, from_cpu, to_cpu](uint32_t acc, size_t) {
                float from_trend = parallel_state_.util_trends_[from_cpu];
                float to_trend = parallel_state_.util_trends_[to_cpu];

                if (std::abs(to_trend) < std::abs(from_trend)) {
                    return acc + 200u;  // 目标核心趋势更稳定
                }
                return acc;
            },
            [](uint32_t a, uint32_t b) { return a + b; }
        );

        return type_saving + load_saving + trend_saving;
    }

    // 获取基础引擎
    MigrationEngineV2& base_engine() { return base_engine_; }
    const MigrationEngineV2& base_engine() const { return base_engine_; }

    // 获取配置
    const LoadConfig& config() const { return load_config_; }
    void set_config(const LoadConfig& config) { load_config_ = config; }

    // 获取并行状态
    const ParallelState& parallel_state() const { return parallel_state_; }

    // 重置统计
    void reset_stats() noexcept {
        base_engine_.reset_stats();
        parallel_state_.util_trends_.fill(0.0f);
        parallel_state_.migration_scores_.fill(0);
    }
};

} // namespace hp::device
