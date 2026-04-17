#pragma once
#include "device/hardware_analyzer.h"
#include <array>
#include <chrono>
#include <optional>
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

class MigrationEngine {
public:
    // 初始化配置文件
    void init(const HardwareProfile& p) noexcept {
        prof_ = p;
        loads_.fill({});
        reset_stats();
    }
    
    // 更新负载 (使用 EMA 平滑)
    void update(int cpu, uint32_t util, uint32_t rq) noexcept;
    
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
    [[nodiscard]] std::span<const CoreLoad, 8> load_snapshot() const noexcept {
        return loads_;
    }
    
    // 动态调整冷却期
    void set_cooling(uint8_t cool) noexcept { cool_ = cool; }
    
    // 观察负载趋势 (返回负载变化率)
    [[nodiscard]] float get_util_trend(int cpu) const noexcept;

private:
    HardwareProfile prof_;
    
    // Modern C++: 强类型结构体 + 内存对齐
    struct CoreLoad {
        uint32_t util{0};      // 0-1024
        uint32_t run_queue{0}; // 0-255
    };
    
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
        std::chrono::steady_clock::time_point last_update;
        float velocity{0.0f};  // 负载变化速度
    };
    std::array<TrendData, 8> trend_cache_{};
    
    // Modern C++: 帮助函数
    void reset_stats() noexcept;
    [[nodiscard]] uint8_t get_cooling_period(bool thermal, bool game) const noexcept;
    [[nodiscard]] std::optional<int> find_best_cpu(CoreRole role, uint32_t max_rq) const noexcept;
    [[nodiscard]] bool should_migrate(float util_norm, uint32_t rq, bool game) const noexcept;
    void update_trend(int cpu, uint32_t util) noexcept;
};

} // namespace hp::device