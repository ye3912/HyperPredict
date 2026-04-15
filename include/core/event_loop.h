#pragma once
#include <atomic>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <algorithm>
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
    
    static constexpr int MIN_PERIOD_MS = 20;
    static constexpr int MAX_PERIOD_MS = 200;
    static constexpr int DEFAULT_PERIOD_MS = 100;
    int current_period_ms_{DEFAULT_PERIOD_MS};
    uint32_t idle_counter_{0};
    uint32_t loop_count_{0};
    
    // FAS 比例修正
    uint32_t target_frame_time_us_{16666};
    static constexpr int32_t FAS_THRESHOLD = 2000;
    static constexpr float FAS_DAILY_GAIN = 0.08f;
    static constexpr float FAS_GAME_GAIN = 0.15f;
    
    // 场景感知
    bool is_gaming_mode_{false};
    Timestamp last_scene_check_{0};
    static constexpr Timestamp SCENE_CHECK_INTERVAL_NS = 2000000000ULL;

    void setup_epoll() noexcept;
    void setup_timer() noexcept;
    void collect_features() noexcept;
    void detect_scene(const LoadFeature& f) noexcept;
    void process_decisions() noexcept;
    void adjust_period(bool urgent) noexcept;
    void cleanup() noexcept;

public:
    void start();
    void stop() { run_.store(false, std::memory_order_release); }
    bool is_running() const noexcept { return run_.load(std::memory_order_acquire); }
    void save_model() noexcept;
    void export_model_json() noexcept;
};

} // namespace hp