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

#include <atomic>
#include <cstdint>

namespace hp {

class EventLoop {
public:
    EventLoop();
    bool init() noexcept;
    void start() noexcept;
    
private:
    void collect() noexcept;
    void process() noexcept;
    void cleanup() noexcept;
    void save() noexcept;
    void adjust(bool increase) noexcept;
    bool is_gaming_scene(const LoadFeature& f) noexcept;
    
    // ✅ 新增：FAS 增量计算（声明）
    int32_t calculate_fas_delta(const LoadFeature& f, float current_fps, 
                                 float target_fps) noexcept;
    
    // 应用频率配置
    void apply_freq_config(const FreqConfig& cfg, 
                          const device::FreqDomain& domain) noexcept;
    
    // 成员变量
    int epfd_;
    int timer_fd_;
    uint32_t period_ms_;
    uint32_t loop_count_;          // ✅ 新增：循环计数器
    uint32_t idle_count_;          // ✅ 新增：空闲计数器
    std::atomic<bool> running_;
    
    // 组件
    SystemCollector collector_;
    device::CpuTopology topo_;
    device::CpuFreqManager freq_mgr_;
    device::HardwareAnalyzer hw_;
    device::MigrationEngine migrator_;
    device::CoreBinder binder_;
    sched::PolicyEngine engine_;
    predict::Predictor predictor_;
    BootCalibrator calibrator_;
    
    // 队列
    LockFreeQueue<LoadFeature, 64> queue_;
};

} // namespace hp