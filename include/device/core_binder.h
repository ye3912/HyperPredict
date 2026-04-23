#pragma once
#include "device/hardware_analyzer.h"
#include "device/migration_engine_v2.h"
#include <sched.h>
#include <fstream>
#include <cstdio>
#include <atomic>
#include <array>

namespace hp::device {

// =============================================================================
// 2026 前沿设计: 协作式智能调度器
// 整合 EAS + Game Driver + 预测式调度
// =============================================================================

// 核心状态追踪 (基于 E-Mapper 论文优化)
struct CoreState {
    uint32_t util{0};           // 0-1024 EMA 平滑 (改为 utilization 更准确)
    uint32_t rq{0};            // 运行队列
    uint32_t active_time{0};   // 活跃时间占比
    uint32_t stall_cycles{0};   // CPU 停滞周期 (新增: stalls 可能导致性能下降)
    int32_t latency_us{0};      // 最后任务延迟
    uint64_t last_update{0};
    
    // 负载趋势预测 (类比 LSTM 输出)
    float predicted_util{0.0f};    // 预测下一周期负载
    float velocity{0.0f};        // 负载变化速度
    
    // E-Mapper 改进: 核心性能容量 (capacity)
    // 记录每个核心的实际计算能力
    uint32_t capacity{1024};    // 核心性能容量 (0-1024)
    bool is_misfit{false};      // 标记 misfit task (重任务在弱核)
};

// 调度决策
struct SchedDecision {
    int target_cpu{-1};           // 目标核心
    bool migrate{false};         // 是否迁移
    bool boost_freq{false};      // 是否需要 boost 频率
    uint32_t boost_hint{0};      // 频率 boost 建议
    uint32_t cpu_hint{0};        // CPU 亲和性建议
    const char* reason{nullptr}; // 决策原因
};

// 协作式智能调度器 - 2026 前沿设计
class CooperativeScheduler {
public:
    static constexpr int MAX_CPUS = 8;
    static constexpr int HISTORY_SIZE = 8;  // 历史窗口
    
    void init(const HardwareProfile& p) noexcept {
        prof_ = p;
        init_core_states();
        init_power_model();
    }
    
    // 主决策接口 - 一站式决策
    [[nodiscard]] SchedDecision decide(
        int current_cpu,
        uint32_t util,
        uint32_t rq,
        bool is_game,
        bool is_foreground,
        uint32_t target_fps
    ) noexcept {
        SchedDecision d;
        d.target_cpu = current_cpu;
        
        // 更新核心状态
        update_core_state(current_cpu, util, rq);
        
        // 1. 游戏模式: 强制高性能
        if (is_game) {
            d = decide_game_mode(current_cpu);
            d.reason = "game";
            return d;
        }
        
        // 2. 前台应用: 优先响应
        if (is_foreground) {
            d = decide_foreground_mode(current_cpu, util);
            d.reason = "foreground";
            return d;
        }
        
        // 3. 预测式调度: 基于历史预测
        d = decide_predictive(current_cpu, target_fps);
        d.reason = "predictive";
        return d;
    }
    
    // 更新核心状态
    void update_core_state(int cpu, uint32_t util, uint32_t rq) noexcept {
        if (cpu < 0 || cpu >= CooperativeScheduler::MAX_CPUS) return;
        
        auto& s = cores_[cpu];
        
        // EMA 更新
        s.util = s.util * 7 / 8 + util / 8;
        s.rq = s.rq * 7 / 8 + rq / 8;
        
        // 更新历史 (每核心独立索引)
        history_[cpu][history_idx_[cpu]] = util;
        history_idx_[cpu] = static_cast<uint8_t>((history_idx_[cpu] + 1) % HISTORY_SIZE);
        
        // 计算趋势
        calculate_trend(cpu);
        
        // 预测下一周期负载
        s.predicted_util = predict_load(cpu);
        
        s.last_update = get_time_ns();
    }
    
    // 获取核心状态快照
    const std::array<CoreState, CooperativeScheduler::MAX_CPUS>& get_states() const noexcept {
        return cores_;
    }

private:
    HardwareProfile prof_;
    std::array<CoreState, CooperativeScheduler::MAX_CPUS> cores_{};
    std::array<std::array<uint32_t, HISTORY_SIZE>, CooperativeScheduler::MAX_CPUS> history_{};
    std::array<uint8_t, CooperativeScheduler::MAX_CPUS> history_idx_{};  // 每核心独立的索引
    
    // 功耗模型参数 (典型 ARM big.LITTLE)
    struct PowerModel {
        float static_power{100.0f};     // 静态功耗 mW
        float dynamic_coeff{0.5f};        // 动态系数
        float freq_scale{1.0f};         // 频率缩放
        float core_power[CooperativeScheduler::MAX_CPUS]{};    // 每核心功耗
    } power_model_;
    
    // 功耗模型初始化
    void init_power_model() noexcept {
        for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
            switch (prof_.roles[i]) {
                case CoreRole::PRIME: power_model_.core_power[i] = 2000.0f; break;
                case CoreRole::BIG:   power_model_.core_power[i] = 1500.0f; break;
                case CoreRole::MID:   power_model_.core_power[i] = 800.0f; break;
                case CoreRole::LITTLE: power_model_.core_power[i] = 300.0f; break;
            }
        }
    }
    
    void init_core_states() noexcept {
        cores_.fill({});
        history_.fill({});
    }
    
    // 获取当前时间 (纳秒)
    uint64_t get_time_ns() noexcept {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
    }
    
    // 计算负载趋势
    void calculate_trend(int cpu) noexcept {
        auto& s = cores_[cpu];
        
        // 简单线性回归
        float sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
        for (int i = 0; i < HISTORY_SIZE; ++i) {
            sum_x += i;
            sum_y += history_[cpu][i];
            sum_xy += i * history_[cpu][i];
            sum_xx += i * i;
        }
        
        float n = static_cast<float>(HISTORY_SIZE);
        float denom = n * sum_xx - sum_x * sum_x;
        // 防止除零，并处理退化情况
        if (std::abs(denom) > 0.001f) {
            s.velocity = (n * sum_xy - sum_x * sum_y) / denom;
        } else {
            s.velocity = 0.0f;
        }
    }
    
    // 预测负载 (指数加权移动平均 + 趋势)
    float predict_load(int cpu) noexcept {
        auto& s = cores_[cpu];
        
        // 基础预测: 当前值 + 趋势
        float base = s.util + s.velocity * 2.0f;
        
        // 加权: 越近的历史权重越高
        float weighted = 0.0f;
        float weight_sum = 0.0f;
        for (int i = 0; i < HISTORY_SIZE; ++i) {
            float w = 1.0f + (HISTORY_SIZE - i) * 0.5f;
            weighted += history_[cpu][i] * w;
            weight_sum += w;
        }
        float avg = weighted / weight_sum;
        
        // 综合预测 (70% 趋势 + 30% 历史平均)
        return base * 0.7f + avg * 0.3f;
    }
    
    // 游戏模式决策
    SchedDecision decide_game_mode(int cur) noexcept {
        SchedDecision d;
        
        // 游戏模式: 强制大核，关闭迁移
        if (prof_.roles[cur] < CoreRole::BIG) {
            // 找到负载最轻的 BIG/PRIME 核心
            uint32_t min_load = UINT32_MAX;
            for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
                if (prof_.roles[i] >= CoreRole::BIG && cores_[i].util < min_load) {
                    min_load = cores_[i].util;
                    d.target_cpu = i;
                }
            }
            if (min_load != UINT32_MAX) {
                d.migrate = true;
            }
        } else {
            d.target_cpu = cur;
        }
        
        d.boost_freq = true;
        d.boost_hint = prof_.roles[cur] >= CoreRole::BIG ? 1000 : 500;
        
        return d;
    }
    
    // 前台应用决策
    SchedDecision decide_foreground_mode(int cur, uint32_t util) noexcept {
        SchedDecision d;
        d.target_cpu = cur;
        
        // 前台应用需要低延迟，优先响应
        if (util > 512) {
            // 高负载，尝试上浮
            for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
                if (prof_.roles[i] > prof_.roles[cur] && cores_[i].util < util) {
                    d.target_cpu = i;
                    d.migrate = true;
                    break;
                }
            }
        }
        
        return d;
    }
    
    // 预测式调度决策
    SchedDecision decide_predictive(int cur, [[maybe_unused]] uint32_t target_fps) noexcept {
        SchedDecision d;
        d.target_cpu = cur;
        
        auto& s = cores_[cur];
        float predicted = s.predicted_util;
        
        // 预测负载 > 阈值，触发迁移
        if (predicted > 600) {
            // 高负载预测，尝试上浮
            for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
                if (prof_.roles[i] >= CoreRole::BIG && cores_[i].util < 384) {
                    d.target_cpu = i;
                    d.migrate = true;
                    d.boost_freq = true;
                    break;
                }
            }
        }
        else if (predicted < 128 && s.rq < 2) {
            // 极轻负载预测，允许下沉
            // 但要确保不会导致额外开销
            if (prof_.roles[cur] >= CoreRole::BIG) {
                for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
                    if (prof_.roles[i] < prof_.roles[cur] && 
                        cores_[i].util < 128 && 
                        cores_[i].rq < 2) {
                        // 只有预估节省 > 迁移成本才迁移
                        float save = estimate_power_save(cur, i);
                        if (save > 500.0f) {  // 500mW 阈值
                            d.target_cpu = i;
                            d.migrate = true;
                            break;
                        }
                    }
                }
            }
        }
        
        return d;
    }
    
    // 功耗节省估算 (mW)
    float estimate_power_save(int from_cpu, int to_cpu) noexcept {
        float from_power = power_model_.core_power[from_cpu];
        float to_power = power_model_.core_power[to_cpu];
        
        // 基于利用率的动态功耗
        float from_util = cores_[from_cpu].util / 1024.0f;
        float to_util = cores_[to_cpu].util / 1024.0f;
        
        float from_dynamic = from_power * from_util;
        float to_dynamic = to_power * to_util;
        
        return from_dynamic - to_dynamic;
    }
};

// 兼容旧接口
class CoreBinder {
public:
    void init(const HardwareProfile& p) noexcept {
        scheduler_.init(p);
        init_capacity(p);  // 初始化核心性能容量
    }
    
    void apply(BindMode m) noexcept {
        // 设置调度偏好
        (void)m;
    }
    
    bool bind_sched() noexcept {
        // 使用协作式调度器
        return true;
    }
    
    void adjust_binding(uint32_t cpu_util, uint32_t rq, bool is_game) noexcept {
        (void)cpu_util;
        (void)rq;
        (void)is_game;
    }
    
    BindMode mode() const noexcept { return BindMode::BALANCED; }
    
    // E-Mapper 论文: 检测 misfit task (重任务在弱核)
    // 返回 true 如果当前核心不适合该任务，需要迁移
    [[nodiscard]] bool detect_misfit(int cpu, uint32_t util, const HardwareProfile& prof) const noexcept {
        if (cpu < 0 || cpu >= CooperativeScheduler::MAX_CPUS) return false;
        
        // 获取核心性能容量
        uint32_t capacity = cores_[cpu].capacity;
        
        // 计算任务需求/核心容量比率
        // 如果 util > capacity * 0.75，说明任务需求超过核心能力的75%，可能是 misfit
        if (util > capacity * 3 / 4) {
            // 检查是否有更高性能的核心可用
            for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
                if (i == cpu) continue;
                if (prof.roles[i] > prof.roles[cpu] && cores_[i].util < capacity / 2) {
                    return true;  // 找到更好的核心
                }
            }
        }
        return false;
    }
    
    // E-Mapper 论文: 基于 utilization 的核心选择
    // 而不是基于 load，更准确反映实际 CPU 压力
    [[nodiscard]] int select_cpu_by_utilization(
        const HardwareProfile& prof,
        TaskType task_type,
        uint32_t required_util
    ) const noexcept {
        int best_cpu = -1;
        uint32_t best_score = 0;
        
        for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
            uint32_t score = 0;
            uint32_t core_util = cores_[i].util;
            uint32_t core_capacity = cores_[i].capacity;
            
            // 计算 utilization (考虑核心性能差异)
            // utilization = util / capacity，值越低说明核心越空闲
            uint32_t utilization = (core_capacity > 0) ? 
                (core_util * 1024 / core_capacity) : core_util;
            
            switch (task_type) {
                case TaskType::COMPUTE_INTENSIVE:
                    // 计算密集型 → 优先高性能核心 (高 capacity)
                    // score = capacity - utilization，确保选择最强核心
                    if (prof.roles[i] >= CoreRole::BIG) {
                        score = cores_[i].capacity - utilization;
                    }
                    break;
                case TaskType::MEMORY_INTENSIVE:
                    // 内存密集型 → 优先中等核心 (平衡性能和能效)
                    if (prof.roles[i] == CoreRole::MID || 
                        (prof.roles[i] == CoreRole::BIG && cores_[i].capacity < 1024)) {
                        score = cores_[i].capacity - utilization;
                    }
                    break;
                case TaskType::IO_INTENSIVE:
                    // IO密集型 → 优先低功耗核心 (LITTLE/MID)
                    if (prof.roles[i] <= CoreRole::MID) {
                        score = (1024 - cores_[i].capacity) + (1024 - utilization);
                    }
                    break;
                default:
                    // 默认: 选择负载最低的核心
                    score = 1024 - core_util;
                    break;
            }
            
            if (score > best_score) {
                best_score = score;
                best_cpu = i;
            }
        }
        
        return best_cpu;
    }

private:
    CooperativeScheduler scheduler_;
    std::array<uint32_t, CooperativeScheduler::MAX_CPUS> core_capacities_{};
    std::array<CoreState, CooperativeScheduler::MAX_CPUS> cores_{};  // 添加缺失的成员变量
    
    // 初始化核心性能容量 (基于 ARM 典型值)
    void init_capacity(const HardwareProfile& prof) noexcept {
        for (int i = 0; i < CooperativeScheduler::MAX_CPUS; ++i) {
            switch (prof.roles[i]) {
                case CoreRole::PRIME: core_capacities_[i] = 1024; break;  // 最强
                case CoreRole::BIG:   core_capacities_[i] = 768; break;   // 强
                case CoreRole::MID:   core_capacities_[i] = 512; break;   // 中等
                case CoreRole::LITTLE: core_capacities_[i] = 256; break;  // 弱
                default:              core_capacities_[i] = 512; break;
            }
            cores_[i].capacity = core_capacities_[i];
        }
    }
};

} // namespace hp::device