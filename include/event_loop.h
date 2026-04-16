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

#include <atomic> // ✅ 修复：添加缺失的头文件

namespace hp {

class EventLoop {
public:
    EventLoop();
    bool init() noexcept;
    void start() noexcept;
    void stop() noexcept; // ✅ 修复：添加 stop 方法声明，解决 main.cpp 编译错误

private:
    void collect() noexcept;
    void process() noexcept;
    void cleanup() noexcept;
    void save() noexcept;
    void adjust(bool increase) noexcept;
    bool is_gaming_scene(const LoadFeature& f) noexcept;
    
    int32_t calculate_fas_delta(const LoadFeature& f, float current_fps, float target_fps) noexcept;
    void apply_freq_config(const FreqConfig& cfg, const device::FreqDomain& domain) noexcept;

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
};

} // namespace hp