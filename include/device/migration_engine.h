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
        uint32_t util{0};      // 0-1024
        uint32_t run_queue{0}; // 0-255
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

    // 检测设备代数 (865及以前为老旧设备)
    void detect_device_generation() noexcept {
        std::string soc = prof_.soc_name;
        
        // 全大核设备识别 (没有小核)
        if (soc.find("8 Elite") != std::string::npos ||
            soc.find("8 Gen 5") != std::string::npos ||
            soc.find("Dimensity 9") != std::string::npos ||
            soc.find("Dimensity 9400") != std::string::npos) {
            device_gen_ = DeviceGen::Flagship;
            is_all_big_ = true;
            is_legacy_ = false;
        }
        // 老旧设备识别
        else if (soc.find("865") != std::string::npos ||
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
        } else {
            device_gen_ = DeviceGen::Modern;
            is_legacy_ = false;
            is_all_big_ = prof_.is_all_big;  // 从硬件配置获取
        }
        
        LOGI("Migration: Legacy=%s, AllBig=%s", 
             is_legacy_ ? "true" : "false", 
             is_all_big_ ? "true" : "false");
        LOGI("Migration: DeviceGen=%d (Legacy=%s)", 
             static_cast<int>(device_gen_), 
             is_legacy_ ? "true" : "false");    }

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

} // namespace hp::device