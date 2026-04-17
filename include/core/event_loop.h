#pragma once
#include "core/types.h"
#include "core/system_collector.h"
#include "device/cpu_topology.h"
#include "device/cpu_freq_manager.h"
#include "device/hardware_analyzer.h"
#include "device/migration_engine.h"
#include "device/core_binder.h"
#include "sched/policy_engine.h"
#include "predict/predictor.h"
#include "core/boot_calibrator.h"
#include "core/lockfree_queue.h"
#include "net/web_server.h"

#include <atomic>
#include <string>

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
    bool is_gaming_scene(const LoadFeature& f) noexcept;
    
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
    device::MigrationEngine migrator_;
    device::CoreBinder binder_;
    sched::PolicyEngine engine_;
    predict::Predictor predictor_;
    BootCalibrator calibrator_;

    LockFreeQueue<LoadFeature, 64> queue_;

    // Web server
    net::WebServer web_server_;
    std::atomic<uint32_t> current_mode_{0};  // 0=daily, 1=game, 2=turbo
    std::atomic<uint8_t> uclamp_min_{50};
    std::atomic<uint8_t> uclamp_max_{100};
    std::string thermal_preset_{"balanced"};
    
    // Latest feature for web queries
    std::mutex latest_mutex_;
    LoadFeature latest_feature_;

    // Sysfs fd 缓存 - 避免重复 fopen
    struct alignas(64) FreqFdCache {
        int min_freq_fd = -1;
        int max_freq_fd = -1;
        int uclamp_min_fd = -1;
        int uclamp_max_fd = -1;
        uint32_t last_min_freq = 0;
        uint32_t last_max_freq = 0;
        uint8_t last_uclamp_min = 0;
        uint8_t last_uclamp_max = 0;
    };
    FreqFdCache freq_fds_[8];
    bool init_freq_fds() noexcept;
    
    // WebServerDelegate implementation
    net::StatusUpdate get_status() override;
    net::ModelWeights get_model_weights() override;
    bool set_model_weights(const net::ModelWeights& weights) override;
    bool handle_command(const net::WebCommand& cmd) override;
};

} // namespace hp