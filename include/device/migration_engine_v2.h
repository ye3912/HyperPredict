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
#include <cmath>

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

// 任务分类 (用于核心选择)
enum class TaskType : uint8_t {
    COMPUTE_INTENSIVE,
    MEMORY_INTENSIVE,
    IO_INTENSIVE,
    UNKNOWN
};

// MigrationEngine V2 - E-Mapper/MMKP 风格全局优化器
// 独立实现，保留与 V1 相同的接口
class MigrationEngineV2 {
public:
    // 核心负载结构体
    struct alignas(64) CoreLoad {
        uint32_t util{0};
        uint32_t run_queue{0};
        uint32_t wakeups{0};
    };
    
    void init(const HardwareProfile& p) noexcept;
    
    // 更新核心负载
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    
    // 重载版本：包含唤醒次数
    void update(int cpu, uint32_t util, uint32_t rq, uint32_t wakeups) noexcept;

    // E-Mapper 风格决策 (带动态 target_fps)
    [[nodiscard]] MigResult decide(int cur, uint32_t therm, bool game, float target_fps = 60.0f) noexcept;
    
    // 设置 EDP 计算专用 target_fps (与 FAS/PolicyEngine 解耦)
    void set_edp_target_fps(float fps) noexcept { target_fps_for_edp_ = fps; }
    [[nodiscard]] float get_edp_target_fps() const noexcept { return target_fps_for_edp_; }
    
    // 当前核心频率（用于 EDP 计算）
    void set_current_freq(uint32_t freq) noexcept { current_freq_khz_ = freq; }
    [[nodiscard]] uint32_t get_current_freq() const noexcept { return current_freq_khz_; }
    
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
    
    // 获取单个核心负载
    [[nodiscard]] CoreLoad get_load(int cpu) const noexcept {
        return (cpu >= 0 && cpu < 8) ? loads_[cpu] : CoreLoad{};
    }
    
    // 获取所有核心负载
    [[nodiscard]] const std::array<CoreLoad, 8>& get_all_loads() const noexcept { return loads_; }
    
    // 获取核心角色
    [[nodiscard]] CoreRole get_role(int cpu) const noexcept {
        return (cpu >= 0 && cpu < 8) ? prof_.roles[cpu] : CoreRole::LITTLE;
    }
    
    // 获取所有核心角色
    [[nodiscard]] const std::array<CoreRole, 8>& get_all_roles() const noexcept { return prof_.roles; }

private:
    // MMKP EDP 计算 (带频率感知)
    [[nodiscard]] float calc_core_edp(int cpu, float target_fps, uint32_t current_freq) const noexcept;
    [[nodiscard]] float calc_total_edp() const noexcept;
    
    // 容量检查
    [[nodiscard]] bool check_capacity(uint32_t total_util) const noexcept;
    
    // 查找目标 (MMKP 风格)
    [[nodiscard]] std::optional<int> find_mmkp_target(int cur) const noexcept;
    
    // 功耗估算
    [[nodiscard]] uint32_t estimate_power_savings(int from_cpu, int to_cpu, uint32_t util) const noexcept;
    
    // 核心状态 (用于 MMKP)
    struct alignas(64) CoreMetrics {
        uint32_t util{0};
        uint32_t rq{0};
        float edp{0.0f};
        bool overutil{false};
    };
    std::array<CoreMetrics, 8> metrics_{};
    
    // 过载跟踪
    float overutil_ratio_{0.0f};
    float target_fps_{60.0f};        // PolicyEngine 传入的 target_fps
    float target_fps_for_edp_{60.0f};  // EDP 计算专用 (场景化)
    uint32_t current_freq_khz_{0};  // 当前核心频率 (用于 EDP 计算)
    uint32_t sample_count_{0};

    // 防振荡滞环
    struct AntiOscillation {
        float edp_diff_history[4]{0.0f};  // EDP 差值历史
        size_t history_idx{0};
        uint32_t sustain_count{0};  // 持续计数

        void add(float diff) noexcept {
            edp_diff_history[history_idx] = diff;
            history_idx = (history_idx + 1) % 4;
        }

        bool should_migrate() const noexcept {
            // 检查是否持续 >3% 且 >=2 个采样周期
            if (sustain_count < 2) return false;

            float avg_diff = 0.0f;
            for (size_t i = 0; i < 4; ++i) {
                avg_diff += edp_diff_history[i];
            }
            avg_diff /= 4.0f;

            return avg_diff > 0.03f;  // 3% 阈值
        }

        void update(bool migrated) noexcept {
            if (migrated) {
                sustain_count++;
            } else {
                sustain_count = 0;
            }
        }
    };
    AntiOscillation anti_oscillation_;

    // 自适应冷却期
    struct AdaptiveCooling {
        float post_edp_history[8]{0.0f};  // 迁移后 EDP 历史
        float prev_edp_history[8]{0.0f};  // 迁移前 EDP 历史
        size_t history_idx{0};
        uint8_t base_cool{8};  // 基础冷却期

        void add(float post_edp, float prev_edp) noexcept {
            post_edp_history[history_idx] = post_edp;
            prev_edp_history[history_idx] = prev_edp;
            history_idx = (history_idx + 1) % 8;
        }

        float get_success_rate() const noexcept {
            float improved = 0.0f;
            float total = 0.0f;
            for (size_t i = 0; i < 8; ++i) {
                if (prev_edp_history[i] > 0) {
                    total += 1.0f;
                    if (post_edp_history[i] < prev_edp_history[i]) {
                        improved += 1.0f;
                    }
                }
            }
            return total > 0 ? improved / total : 0.5f;
        }

        uint8_t get_adaptive_cool() const noexcept {
            float success_rate = get_success_rate();
            uint8_t cool = base_cool;

            if (success_rate > 0.7f) {
                cool = static_cast<uint8_t>(cool * 0.7f);  // 成功率高，加速调度
            } else if (success_rate < 0.4f) {
                cool = static_cast<uint8_t>(std::min(cool * 1.5f, 12.0f));  // 成功率低，防抖
            }

            return std::max(cool, static_cast<uint8_t>(2));  // 最小 2
        }
    };
    AdaptiveCooling adaptive_cooling_;
    
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

    struct alignas(64) TrendData {
        uint32_t prev_util{0};
        std::chrono::steady_clock::time_point last_update;
        float velocity{0.0f};
    };
    std::array<TrendData, 8> trend_cache_;

    // 动态 EMA 历史数据（用于计算 std_dev 和 mean_util）
    struct UtilHistory {
        static constexpr size_t HISTORY_SIZE = 16;
        std::array<uint32_t, HISTORY_SIZE> util_history{};
        size_t history_idx{0};
        size_t history_count{0};

        void add(uint32_t util) noexcept {
            util_history[history_idx] = util;
            history_idx = (history_idx + 1) % HISTORY_SIZE;
            if (history_count < HISTORY_SIZE) {
                history_count++;
            }
        }

        float get_mean() const noexcept {
            if (history_count == 0) return 0.0f;
            uint64_t sum = 0;
            for (size_t i = 0; i < history_count; ++i) {
                sum += util_history[i];
            }
            return static_cast<float>(sum) / history_count;
        }

        float get_stddev(float mean) const noexcept {
            if (history_count < 2) return 0.0f;
            float variance = 0.0f;
            for (size_t i = 0; i < history_count; ++i) {
                float diff = util_history[i] - mean;
                variance += diff * diff;
            }
            variance /= history_count;
            return std::sqrt(variance);
        }
    };
    std::array<UtilHistory, 8> util_history_;

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

    // 辅助函数：判断两个核心是否在同一个缓存域
    [[nodiscard]] bool in_same_cache_domain(int cpu1, int cpu2) const noexcept {
        if (cpu1 < 0 || cpu1 >= 8 || cpu2 < 0 || cpu2 >= 8) return false;
        return prof_.migration.cache_domain[cpu1] == prof_.migration.cache_domain[cpu2];
    }
};

} // namespace hp::device