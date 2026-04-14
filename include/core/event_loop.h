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
    
    // 性能优化 + 游戏优化
    static constexpr int DECISION_PERIOD_MS = 50;   // 10→50ms (平衡性能/功耗)
    static constexpr int MAX_EVENTS = 4;
    static constexpr int COLLECT_INTERVAL = 10;     // 每500ms采集
    
    static constexpr const char* MODEL_BIN_PATH = "/data/adb/modules/hyperpredict/model.dat";
    static constexpr const char* MODEL_JSON_PATH = "/data/adb/modules/hyperpredict/model.json";

    void setup_epoll() noexcept;
    void setup_timer() noexcept;
    void collect_features() noexcept;
    void process_decisions() noexcept;
    void cleanup() noexcept;

public:
    void start();
    void stop() { run_.store(false, std::memory_order_release); }
    bool is_running() const noexcept { return run_.load(std::memory_order_acquire); }
    void save_model() noexcept;
    void export_model_json() noexcept;
};

} // namespace hp