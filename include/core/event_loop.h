#pragma once
#include "core/types.h"
#include "core/system_collector.h"
#include "device/cpu_topology.h"
#include "device/cpu_freq_manager.h"
#include "device/hardware_analyzer.h"
#include "device/migration_engine_v2.h"
#include "sched/policy_engine.h"
#include "predict/predictor.h"
#include "core/boot_calibrator.h"
#include "net/web_server.h"

#include <atomic>
#include <string>
#include <cstdint>
#include <shared_mutex>

// Rate limiting constant
inline constexpr uint64_t RATE_LIMIT_MIN_US = 1000;  // 1ms min interval

namespace hp {

class EventLoop : public net::WebServerDelegate {
public:
    EventLoop();
    bool init() noexcept;
    void start() noexcept;
    void stop() noexcept;

private:
    void collect() noexcept;
    void process() noexcept;
    void cleanup() noexcept;
    void save() noexcept;
    void adjust(bool increase) noexcept;

    // ========== 新增: 空闲状态检测 ==========
    void check_idle_state(const LoadFeature& f) noexcept;
    void apply_idle_freq() noexcept;
    
    int32_t calculate_fas_delta(const LoadFeature& f, float current_fps, 
                                 float target_fps) noexcept;
    void apply_freq_config(const FreqConfig& cfg, 
                          const device::FreqDomain& domain) noexcept;

    int epfd_;
    int timer_fd_;
    uint32_t period_ms_;
    uint32_t loop_count_;
    uint32_t idle_count_;
    std::atomic<bool> running_;

    SystemCollector collector_;
    device::CpuTopology topo_;
    device::CpuFreqManager freq_mgr_;
    device::HardwareAnalyzer hw_;
    device::MigrationEngineV2 migrator_;
    sched::PolicyEngine engine_;
    predict::Predictor predictor_;
    BootCalibrator calibrator_;

    // collect/process 同线程直接传递，无需队列
    LoadFeature pending_feature_{};
    bool has_pending_feature_{false};

    // Web server
    net::WebServer web_server_;
    std::atomic<uint32_t> current_mode_{0};  // 0=daily, 1=game, 2=turbo
    std::atomic<uint8_t> uclamp_min_{50};
    std::atomic<uint8_t> uclamp_max_{100};
    std::string thermal_preset_{"balanced"};
    
    // Latest feature for web queries (读写分离优化)
    mutable std::shared_mutex latest_mutex_;
    LoadFeature latest_feature_;

    // Sysfs fd 缓存 - 避免重复 fopen
    struct alignas(64) FreqFdCache {
        int min_freq_fd = -1;
        int max_freq_fd = -1;
        int cur_freq_fd = -1;
        int uclamp_min_fd = -1;
        int uclamp_max_fd = -1;
        uint32_t last_min_freq = 0;
        uint32_t last_max_freq = 0;
        uint32_t last_cur_freq = 0;
        uint8_t last_uclamp_min = 0;
        uint8_t last_uclamp_max = 0;
    };
    FreqFdCache freq_fds_[8];
    bool init_freq_fds() noexcept;
    
    // ========== 新增: Rate Limiting ==========
    uint64_t last_freq_update_us_{0};        // 上次调频时间
    uint64_t rate_limit_us_{RATE_LIMIT_MIN_US};  // 当前限速间隔

    // ========== 新增: 空闲状态检测 ==========
    bool is_idle_{false};                    // 是否处于空闲状态
    uint64_t idle_start_time_{0};            // 空闲开始时间
    uint64_t last_touch_time_{0};            // 最后触摸时间
    static constexpr uint64_t IDLE_TOUCH_TIMEOUT_US = 120000000ULL;  // 2分钟无触摸
    static constexpr uint32_t IDLE_LOAD_THRESHOLD = 51;  // 5% 负载阈值 (0-1024)

    // ========== 新增: 逐步下探策略 ==========
    size_t idle_step_{0};                    // 当前下探档位
    uint64_t last_idle_step_time_{0};        // 上次下探时间
    static constexpr uint64_t IDLE_STEP_INTERVAL_US = 30000000ULL;  // P2: 原 60s
    static constexpr size_t IDLE_MAX_STEPS = 5;  // 最大下探档位数

    // ========== 新增: CPU-domain 映射优化 ==========
    std::array<int, 8> cpu_to_domain_map_{-1, -1, -1, -1, -1, -1, -1, -1};  // CPU 到 domain 的映射
    void build_cpu_domain_map() noexcept;  // 构建 CPU-domain 映射
    
    // UCLAMP 回退机制
    enum class SchedBackend {
        UCLAMP,        // 原生 uclamp (cgroup v1)
        CGROUP_V2,     // cgroup v2 cpu.weight
        FREQ_ONLY,     // 仅频率控制
        SCHED_BACKGROUND  // SCHED_IDLE 调度策略 (重命名避免宏冲突)
    };
    SchedBackend sched_backend_{SchedBackend::UCLAMP};
    bool detect_sched_backend() noexcept;
    
    // 频率回退：当 uclamp 不可用时，提高/降低基础频率补偿
    uint32_t get_compensated_freq(uint32_t base_freq, uint8_t uclamp_min) const noexcept;
    
    // WebServerDelegate implementation
    net::StatusUpdate get_status() override;
    net::ModelWeights get_model_weights() override;
    bool set_model_weights(const net::ModelWeights& weights) override;
    bool handle_command(const net::WebCommand& cmd) override;

    // ========== 迁移后源CPU降频 ==========
    void post_migration_freq_adjust(int source_cpu, uint32_t cpu_util) noexcept;

    // ========== 新增: 任务合并优化 ==========
    // 帧采样间隔 (根据场景动态调整)
    static constexpr uint32_t SAMPLE_INTERVAL_IDLE = 5;    // 日常: 每5帧采样一次
    static constexpr uint32_t SAMPLE_INTERVAL_GAME = 2;   // 游戏: 每2帧采样一次
    static constexpr uint32_t SAMPLE_INTERVAL_TRAIN = 30; // 训练触发间隔 (帧数)

    uint32_t last_training_frame_{0};  // 上次训练触发时的帧计数

    // ========== 功耗预算模式 ==========
    struct PowerBudget {
        std::atomic<bool> enabled{false};
        std::atomic<uint32_t> max_power_mw{0};
        std::atomic<uint32_t> current_power_mw{0};

        uint32_t get_freq_cap(uint32_t desired, int role) const noexcept {
            if (!enabled.load(std::memory_order_relaxed)) {
                return desired;
            }
            // role: 0=LITTLE, 1=MID, 2=PRIME/PRIME
            float reduction = (role >= 2) ? 0.8f :
                             (role >= 1) ? 0.9f : 1.0f;
            return static_cast<uint32_t>(desired * reduction);
        }
    };
    PowerBudget power_budget_;
};

} // namespace hp