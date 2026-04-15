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
    int thermal_fd_ = -1;      // ✅ 温度事件
    int fps_fd_ = -1;          // ✅ 帧率事件
    
    // 自适应配置
    static constexpr int MIN_PERIOD_MS = 20;    // 最小周期
    static constexpr int MAX_PERIOD_MS = 200;   // 最大周期
    int current_period_ms_{50};                 // 当前周期
    uint64_t last_decision_time_{0};
    uint32_t idle_counter_{0};
    
    static constexpr int MAX_EVENTS = 8;
    static constexpr int COLLECT_INTERVAL_NORMAL = 10;   // 正常采集
    static constexpr int COLLECT_INTERVAL_IDLE = 40;     // 空闲采集
    
    static constexpr const char* MODEL_BIN_PATH = "/data/adb/modules/hyperpredict/model.dat";
    static constexpr const char* MODEL_JSON_PATH = "/data/adb/modules/hyperpredict/model.json";

    void setup_epoll() noexcept;
    void setup_timer() noexcept;
    void setup_thermal_monitor() noexcept;  // ✅ 新增
    void collect_features() noexcept;
    void process_decisions(bool urgent = false) noexcept;  // ✅ 紧急标志
    void adjust_period(bool urgent) noexcept;  // ✅ 自适应调整
    void cleanup() noexcept;

public:
    void start();
    void stop() { run_.store(false, std::memory_order_release); }
    bool is_running() const noexcept { return run_.load(std::memory_order_acquire); }
    void save_model() noexcept;
    void export_model_json() noexcept;
};

} // namespace hp