#pragma once
#include "core/types.h"
#include <array>
#include <memory>

namespace hp::sched {

// =============================================================================
// 预测器状态 - 增强版
// =============================================================================
struct PredictorState {
    uint64_t last_update{0};
    float ewma_util{0.0f};
    float ewma_fps{0.0f};
    float trend{0.0f};
    float util_slope_50ms{0.0f};
    float boost_prob{0.0f};
    float predicted_util_50ms{0.0f};
};

// SchedHorizon 频率模式
enum class FreqMode {
    POWERSAVE,     // margin=300MHz
    BALANCE,       // margin=200MHz
    PERFORMANCE,  // margin=100MHz
    FAST          // margin=0MHz
};

struct ConfigHistory {
    uint64_t last{0};
    FreqConfig cfg{};
    uint32_t cfg_hash{0};
};

// =============================================================================
// PolicyEngine - 增强版调度策略引擎
// 类比 CNN 论文: 预训练模型 + 在线推理 + 多因素协同决策
// =============================================================================
class PolicyEngine {
private:
    // 前向声明实现类
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    BaselinePolicy baseline_{};
    PredictorState pred_state_{};
    std::array<ConfigHistory, 3> hist_{};
    uint32_t loop_count_{0};
    
public:
    PolicyEngine() noexcept;
    ~PolicyEngine() noexcept;
    
    // 禁用拷贝
    PolicyEngine(const PolicyEngine&) = delete;
    PolicyEngine& operator=(const PolicyEngine&) = delete;
    
public:
    // 初始化
    void init(const BaselinePolicy& baseline) noexcept;

    // 设置最低频率 (空闲时可下探到此频率)
    void set_min_freq(uint32_t min_freq_khz) noexcept;
    
    // 设置 EMA 权重 (用于日常/视频场景)
    void set_ema_weights(float short_alpha, float medium_alpha, float long_alpha) noexcept;
    
    // SchedHorizon 模式设置
    void set_freq_mode(FreqMode mode) noexcept;
    uint32_t get_freq_margin() const noexcept;
    
    // 核心决策
    FreqConfig decide(const LoadFeature& f, float target_fps, const char* scene) noexcept;
    
    // 模型导出
    void export_model(const char* path) noexcept;
    
    // ========== 新增接口 ==========
    
    // IO-Wait Boost 控制
    void set_io_wait_boost(bool has_iowait) noexcept;
    uint32_t get_io_wait_boost() const noexcept;
    
    // 帧渲染感知
    void on_frame_end() noexcept;
    
    // Rate Limiting 查询
    bool should_update_freq(uint64_t now_ns) const noexcept;
    void update_freq_timestamp(uint64_t now_ns) noexcept;
    
    // 趋势查询
    float get_util_trend() const noexcept;
    
    // ========== E-Mapper 风格 Over-utilization 跟踪 ==========
    enum class UtilStage { Initial, Measured, Mature };
    
    // 状态查询
    bool is_overutilized() const noexcept;
    UtilStage get_util_stage() const noexcept;
    float get_overutil_ratio() const noexcept;
};

} // namespace hp::sched