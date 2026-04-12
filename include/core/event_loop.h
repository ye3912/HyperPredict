#pragma once
#include <atomic>
#include <thread>
#include "core/lockfree_queue.h"
#include "core/boot_calibrator.h"
#include "sched/policy_engine.h"
#include "kernel/sysfs_writer.h"

namespace hp {
class EventLoop {
    std::atomic<bool> run_{true};
    FeatureQueue q_;
    kernel::SysfsWriter writer_;
    sched::PolicyEngine engine_;
    BootCalibrator calibrator_;
    void collect();
    void dispatch();
public:
    void start();
    void stop() { run_.store(false); }
};
} // namespace hp