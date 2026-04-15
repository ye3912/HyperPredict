#pragma once
#include <atomic>
#include <sys/epoll.h>
#include <sys/timerfd.h>
<<<<<<< HEAD
=======
#include <algorithm>
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
#include "core/lockfree_queue.h"
#include "core/boot_calibrator.h"
#include "sched/policy_engine.h"
#include "kernel/sysfs_writer.h"
#include "predict/feature_extractor.h"
#include "device/cpu_topology.h"
#include "device/cpu_freq_manager.h"
<<<<<<< HEAD
#include "device/hardware_analyzer.h"
#include "device/migration_engine.h"
#include "device/core_binder.h"
#include "core/system_collector.h"

namespace hp {
=======
#include "core/system_collector.h"

namespace hp {

>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
class EventLoop {
    std::atomic<bool> run_{true};
    FeatureQueue q_;
    kernel::SysfsWriter writer_;
    sched::PolicyEngine engine_;
    BootCalibrator calibrator_;
    predict::FeatureExtractor extractor_;
    device::CpuTopology topology_;
<<<<<<< HEAD
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
=======
    device::CpuFreqManager freq_manager_;
    SystemCollector collector_;

    int epfd_ = -1;
    int timer_fd_ = -1;
    
    static constexpr int MIN_PERIOD_MS = 20;
    static constexpr int MAX_PERIOD_MS = 200;
    static constexpr int DEFAULT_PERIOD_MS = 100;
    int current_period_ms_{DEFAULT_PERIOD_MS};
    uint32_t idle_counter_{0};
    uint32_t loop_count_{0};
    
    uint32_t target_frame_time_us_{16666};
    static constexpr int32_t FAS_THRESHOLD = 2000;
    static constexpr float FAS_DAILY_GAIN = 0.08f;
    static constexpr float FAS_GAME_GAIN = 0.15f;
    
    bool is_gaming_mode_{false};
    Timestamp last_scene_check_{0};
    static constexpr Timestamp SCENE_CHECK_INTERVAL_NS = 2000000000ULL;
    
    static constexpr const char* MODEL_BIN_PATH = "/data/adb/modules/hyperpredict/model.dat";
    static constexpr const char* MODEL_JSON_PATH = "/data/adb/modules/hyperpredict/model.json";

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
>>>>>>> 0c63a1712e0bb23f2a76ffafe2e7a2000ce80799
