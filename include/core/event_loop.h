#pragma once
#include <atomic>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "core/lockfree_queue.h"
#include "core/boot_calibrator.h"
#include "sched/policy_engine.h"
#include "kernel/sysfs_writer.h"
#include "predict/feature_extractor.h"
#include "device/cpu_topology.h"
#include "device/cpu_freq_manager.h"
#include "device/hardware_analyzer.h"
#include "device/migration_engine.h"
#include "device/core_binder.h"
#include "core/system_collector.h"

namespace hp {
class EventLoop {
    std::atomic<bool> run_{true};
    FeatureQueue q_;
    kernel::SysfsWriter writer_;
    sched::PolicyEngine engine_;
    BootCalibrator calibrator_;
    predict::FeatureExtractor extractor_;
    device::CpuTopology topology_;
    device::FreqManager freq_mgr_;
    device::HardwareAnalyzer hw_;
    device::MigrationEngine migrator_;
    device::CoreBinder binder_;
    SystemCollector collector_;
    int epfd_=-1, timer_fd_=-1;
    int period_ms_{100}; uint32_t idle_cnt_{0}, loop_{0}, mig_tick_{0};
    uint32_t target_frame_us_{16666};
    static constexpr int32_t FAS_THR=2000;
    static constexpr const char* MODEL_PATH="/data/adb/modules/hyperpredict/model.dat";
    void setup_epoll() noexcept; void setup_timer() noexcept;
    void collect() noexcept; void detect_scene(const LoadFeature& f) noexcept;
    void process() noexcept; void adjust(bool u) noexcept; void cleanup() noexcept;
    void init_hw() noexcept; void check_mode() noexcept;
public:
    void start(); void stop() { run_.store(false, std::memory_order_release); }
    void save() noexcept { engine_.export_model(MODEL_PATH); }
};
}