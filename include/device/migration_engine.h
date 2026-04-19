#pragma once
#include "device/hardware_analyzer.h"
#include "core/logger.h"
#include <string>
#include <array>
#include <chrono>
#include <optional>
#include <cstdint>
#include <span>

namespace hp::device {

struct MigResult {
    int target{-1};
    bool go{false};
    bool thermal{false};
};

// Modern C++: 使用强类型枚举和 constexpr
enum class MigPolicy : uint8_t {
    Conservative,  // 保守模式 - 日常/省电
    Balanced,      // 平衡模式 - 默认
    Aggressive     // 激进模式 - 游戏/性能
};

// 设备代数识别 (用于优化老旧设备的迁移策略)
enum class DeviceGen : uint8_t {
    Legacy,     // 老旧设备 (865及以前)
    Modern,     // 现代设备 (870/888/8Gen1+)
    Flagship    // 旗舰设备 (8Gen2/3/ Elite)
};

class MigrationEngine {
public:
    // 初始化配置文件
    void init(const HardwareProfile& p) noexcept {
        prof_ = p;
        loads_.fill({});
        reset_stats();
        detect_device_generation();
        configure_all_big_optimization();
    }

    // 更新负载 (使用 EMA 平滑)
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    
    // 重载版本：包含唤醒次数（用于任务分类）
    void update(int cpu, uint32_t util, uint32_t rq, uint32_t wakeups) noexcept;

    // 决策迁移 - 支持动态策略调整
    [[nodiscard]] MigResult decide(int cur, uint32_t therm, bool game) noexcept;

    // 重置
    void reset() noexcept {
        loads_.fill({});
        cool_ = 0;
        reset_stats();
    }

    // Modern C++: 设置迁移策略 (运行时多态)
    void set_policy(MigPolicy p) noexcept { policy_ = p; }
    [[nodiscard]] MigPolicy policy() const noexcept { return policy_; }

    // Modern C++: 获取核心负载状态快照 (用于调试)
    // 使用 auto 推断返回类型
    [[nodiscard]] auto load_snapshot() const noexcept {
        return loads_;
    }

    // 动态调整冷却期
    void set_cooling(uint8_t cool) noexcept { cool_ = cool; }

    // 观察负载趋势 (返回负载变化率)
    [[nodiscard]] float get_util_trend(int cpu) const noexcept;

    // Modern C++: 强类型结构体 + 内存对齐
    struct CoreLoad {
        uint32_t util{0};       // 0-1024
        uint32_t run_queue{0};  // 0-255
        uint32_t wakeups{0};    // 0-1000 (唤醒次数)
    };

private:
    HardwareProfile prof_;

    std::array<CoreLoad, 8> loads_{};
    uint8_t cool_{0};

    // Modern C++: 策略模式
    MigPolicy policy_{MigPolicy::Balanced};

    // 动态冷却期 (基于策略)
    static constexpr uint8_t COOL_THERMAL = 4;
    static constexpr uint8_t COOL_CONSERVATIVE = 12;
    static constexpr uint8_t COOL_BALANCED = 8;
    static constexpr uint8_t COOL_AGGRESSIVE = 4;

    // 历史趋势缓存
    struct TrendData {
        uint32_t prev_util{0};
        std::chrono::steady_clock::time_point last_update;        float velocity{0.0f};  // 负载变化速度
    };
    std::array<TrendData, 8> trend_cache_{};

    // 设备代数识别
    DeviceGen device_gen_{DeviceGen::Modern};
    bool is_legacy_{false};           // 老旧设备 (865及以前)
    bool is_all_big_{false};          // 全大核设备 (8 Elite, 9400等)
    
    // 全大核设备优化参数
    struct AllBigConfig {
        bool enabled{false};          // 是否启用全大核优化
        bool has_prime_cores{false};   // 是否有超大核
        uint8_t prime_count{0};       // 超大核数量
        uint8_t perf_count{0};         // 性能核数量
        float freq_ratio{1.0f};       // 最高/最低频率比
        uint32_t low_util_thresh{256}; // 低负载阈值
        uint32_t high_util_thresh{512}; // 高负载阈值
        uint32_t migration_cool{4};   // 迁移冷却期
    } all_big_config_;

    // 检测设备代数 (865及以前为老旧设备)
    void detect_device_generation() noexcept;
    
    // 全大核设备优化
    void configure_all_big_optimization() noexcept;
    
    // 智能线程放置 (全大核设备)
    [[nodiscard]] int select_thread_placement(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept;
    
    // 全大核设备核间迁移
    [[nodiscard]] std::optional<int> find_all_big_target(int cur, uint32_t util, uint32_t rq, bool is_game) const noexcept;
    
    // Modern C++: 帮助函数
    void reset_stats() noexcept;
    [[nodiscard]] uint8_t get_cooling_period(bool thermal, bool game) const noexcept;
    [[nodiscard]] std::optional<int> find_best_cpu(CoreRole role, uint32_t max_rq) const noexcept;
    [[nodiscard]] bool should_migrate(float util_norm, uint32_t rq, bool game) const noexcept;
    void update_trend(int cpu, uint32_t util) noexcept;

    // 功耗估算函数
    [[nodiscard]] uint32_t estimate_power_savings(int from_cpu, int to_cpu, uint32_t util) noexcept;

    // 老旧设备专用: 小核→中核迁移
    [[nodiscard]] bool should_promote_to_mid(int cur, uint32_t util, uint32_t rq) const noexcept;

    // 老旧设备专用: 中核→小核下沉
    [[nodiscard]] bool should_demote_to_little(int cur, uint32_t util, uint32_t rq) const noexcept;
};

// 任务分类枚举 (基于 E-Mapper 论文)
enum class TaskType {
    COMPUTE_INTENSIVE,  // 计算密集型
    MEMORY_INTENSIVE,   // 内存密集型
    IO_INTENSIVE,       // IO密集型
    UNKNOWN             // 未知
};

} // namespace hp::device