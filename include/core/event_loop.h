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
    SystemCollector collector_;

    int epfd_ = -1;
    int timer_fd_ = -1;
    static constexpr int MAX_EVENTS = 4;
    static constexpr int DECISION_PERIOD_MS = 10;

    void setup_epoll() noexcept;
    void setup_timer() noexcept;
    void collect_features() noexcept;
    void process_decisions() noexcept;
    void cleanup() noexcept;

public:
    void start();
    void stop() { run_.store(false, std::memory_order_release); }
    bool is_running() const noexcept { return run_.load(std::memory_order_acquire); }
};

} // namespace hp