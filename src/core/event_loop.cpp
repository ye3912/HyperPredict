#include "core/event_loop.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sched.h>
#include <algorithm>
#include <chrono>

namespace hp {

// =============================================================================
// Rate Limiting 常量 - 类比 CNN 论文的查表化简
// =============================================================================
static constexpr uint64_t RATE_LIMIT_MIN_US = 1000ULL;  // 1ms 最小调频间隔
static constexpr uint64_t RATE_LIMIT_GAME_US = 500ULL; // 游戏模式 500us
static constexpr uint64_t RATE_LIMIT_DAILY_US = 2000ULL; // 日常模式 2ms

EventLoop::EventLoop()
    : epfd_(-1)
    , timer_fd_(-1)
    , period_ms_(100)
    , loop_count_(0)
    , idle_count_(0)
    , running_(false)
    , web_server_(net::DEFAULT_PORT)
    , last_freq_update_us_(0) {
}

// ✅ 新增：实现 stop 方法
void EventLoop::stop() noexcept {
    running_ = false;
}

bool EventLoop::init() noexcept {
    LOGI("=== HyperPredict v4.2 Initializing ===");
    
    if (!hw_.analyze()) {
        LOGE("Hardware analysis failed");
        return false;
    }
    
    if (!topo_.detect()) {
        LOGE("Topology detection failed");
        return false;
    }
    
    if (!freq_mgr_.init()) {
        LOGE("FreqManager init failed");
        return false;
    }
    
    // 检测调度后端
    detect_sched_backend();
    
    migrator_.init(hw_.profile());
    binder_.init(hw_.profile());
    binder_.bind_sched();    
    calibrator_.calibrate(topo_);
    engine_.init(calibrator_.baseline());
    
    // Start Web Server
    web_server_.set_delegate(this);
    if (!web_server_.start()) {
        LOGW("Web server failed to start (port may be in use)");
    } else {
        LOGI("Web server listening on port %u", web_server_.port());
    }
    
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) {
        LOGE("epoll_create1 failed: %s", strerror(errno));
        return false;
    }
    
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timer_fd_ < 0) {
        LOGE("timerfd_create failed: %s", strerror(errno));
        close(epfd_);
        return false;
    }
    
    struct itimerspec its{};
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = period_ms_ * 1000000;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = period_ms_ * 1000000;
    
    if (timerfd_settime(timer_fd_, 0, &its, nullptr) < 0) {
        LOGE("timerfd_settime failed: %s", strerror(errno));
        close(timer_fd_);
        close(epfd_);
        return false;
    }
    
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd_;
    
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, timer_fd_, &ev) < 0) {
        LOGE("epoll_ctl failed: %s", strerror(errno));
        close(timer_fd_);
        close(epfd_);
        return false;
    }
    
    LOGI("Initialization complete | Period=%ums | Cores=%d", 
         period_ms_, topo_.get_total_cpus());
    
    return true;
}

void EventLoop::collect() noexcept {
    LoadFeature f = collector_.collect();
    
    // ========== 新增: IO-Wait 检测 ==========
    // 检测 IO 密集型任务 (高唤醒次数但 CPU 利用率低)
    if (f.wakeups_100ms > 80 && f.cpu_util < 300) {
        io_wait_detected_++;
        // 传递给 predictor
        predictor_.io_wait_manager().update(true, 0);
    } else {
        io_wait_detected_ = 0;
        predictor_.io_wait_manager().update(false, 0);
    }
    
    // ========== 新增: 多时间尺度特征更新 ==========
    // 更新 predictor 的多时间尺度特征
    auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    predictor_.update_multi_scale_features(f, now_ns);
    
    if (!queue_.try_push(f)) {
        LOGW("Queue full, dropping frame");
    }
    
    // ========== 新增: 异步训练 ==========
    // 在后台线程中进行训练，不阻塞主循环
    if (loop_count_ % 10 == 0 && !predictor_.is_training()) {
        // 每 10 个周期触发一次异步训练
        float actual_fps = f.frame_interval_us > 0 ? 
            1000000.0f / static_cast<float>(f.frame_interval_us) : 60.0f;
        predictor_.train_async(f, actual_fps);
    }
}

bool EventLoop::is_gaming_scene(const LoadFeature& f) noexcept {
    if (f.is_gaming) return true;
    
    float fps = 1000000.0f / static_cast<float>(f.frame_interval_us);
    if (fps > 90.0f && f.touch_rate_100ms > 30) {
        return true;
    }
    
    if (f.cpu_util > 800 && f.thermal_margin < 10) {
        return true;
    }
    
    return false;
}

int32_t EventLoop::calculate_fas_delta(const LoadFeature& f, float current_fps, 
                                        float target_fps) noexcept {
    (void)f;
    
    static int32_t last_delta = 0;
    
    float fps_error = target_fps - current_fps;
    int32_t delta = static_cast<int32_t>(fps_error * 10000.0f);
    delta = static_cast<int32_t>(last_delta * 0.7f + delta * 0.3f);
    delta = std::clamp(delta, -300000, 300000);
    
    if (std::abs(delta) < 50000) {
        delta = 0;
    }
    
    last_delta = delta;
    return delta;
}

void EventLoop::apply_freq_config(const FreqConfig& cfg, 
                                   const device::FreqDomain& domain) noexcept {
    for (int cpu : domain.cpus) {
        if (cpu < 0 || cpu >= 8) continue;
        auto& fc = freq_fds_[cpu];
        
        // 计算补偿频率 (uclamp 不可用时)
        uint32_t effective_min_freq = cfg.min_freq;
        uint32_t effective_max_freq = cfg.target_freq;
        
        if (sched_backend_ != SchedBackend::UCLAMP) {
            // 无 uclamp 支持时，使用频率补偿
            if (sched_backend_ == SchedBackend::CGROUP_V2) {
                // 尝试写入 cgroup v2 cpu.weight
                char cgroup_path[256];
                snprintf(cgroup_path, sizeof(cgroup_path),
                    "/sys/fs/cgroup/system/cpu%d/cpu.weight", cpu);
                if (access(cgroup_path, F_OK) == 0) {
                    FILE* f = fopen(cgroup_path, "w");
                    if (f) {
                        fprintf(f, "%u\n", 100 + cfg.uclamp_min * 100);
                        fclose(f);
                    }
                }
            }
            
            // 频率补偿：当 uclamp 限制低优先级时，提高频率上限
            if (cfg.uclamp_min < 50) {
                // 低 uclamp -> 提高频率上限来补偿
                uint32_t boost = domain.max_freq * (50 - cfg.uclamp_min) / 200;
                effective_max_freq = std::min(domain.max_freq, cfg.target_freq + boost);
            }
            
            // 静态调试日志（每 50 次输出一次）
            static int log_count = 0;
            if (++log_count % 50 == 1) {
                LOGD("Freq fallback: Backend=%d | UCLAMP=%u/%u | Freq=%u/%u",
                    static_cast<int>(sched_backend_),
                    cfg.uclamp_min, cfg.uclamp_max,
                    effective_min_freq, effective_max_freq);
            }
        }
        
        // 值缓存 - 避免重复写入相同值
        if (fc.last_min_freq != effective_min_freq) {
            if (fc.min_freq_fd < 0) {
                char path[128];
                snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
                fc.min_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
            }
            if (fc.min_freq_fd >= 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%u\n", effective_min_freq);
                ::write(fc.min_freq_fd, buf, len);
                fc.last_min_freq = effective_min_freq;
            }
        }
        
        if (fc.last_max_freq != effective_max_freq) {
            if (fc.max_freq_fd < 0) {
                char path[128];
                snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
                fc.max_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
            }
            if (fc.max_freq_fd >= 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%u\n", effective_max_freq);
                ::write(fc.max_freq_fd, buf, len);
                fc.last_max_freq = effective_max_freq;
            }
        }
        
        // UCLAMP 写入 (仅当支持时)
        if (sched_backend_ == SchedBackend::UCLAMP) {
            if (fc.last_uclamp_min != cfg.uclamp_min) {
                if (fc.uclamp_min_fd < 0) {
                    char path[128];
                    snprintf(path, sizeof(path), "/dev/cpuctl/cpu%d/uclamp.min", cpu);
                    fc.uclamp_min_fd = ::open(path, O_WRONLY | O_CLOEXEC);
                }
                if (fc.uclamp_min_fd >= 0) {
                    char buf[8];
                    int len = snprintf(buf, sizeof(buf), "%u\n", cfg.uclamp_min);
                    ::write(fc.uclamp_min_fd, buf, len);
                    fc.last_uclamp_min = cfg.uclamp_min;
                }
            }
            
            if (fc.last_uclamp_max != cfg.uclamp_max) {
                if (fc.uclamp_max_fd < 0) {
                    char path[128];
                    snprintf(path, sizeof(path), "/dev/cpuctl/cpu%d/uclamp.max", cpu);
                    fc.uclamp_max_fd = ::open(path, O_WRONLY | O_CLOEXEC);
                }
                if (fc.uclamp_max_fd >= 0) {
                    char buf[8];
                    int len = snprintf(buf, sizeof(buf), "%u\n", cfg.uclamp_max);
                    ::write(fc.uclamp_max_fd, buf, len);
                    fc.last_uclamp_max = cfg.uclamp_max;
                }
            }
        }
    }
}

void EventLoop::process() noexcept {
    auto f_opt = queue_.try_pop();
    if (!f_opt) return;
    
    const LoadFeature& f = *f_opt;
    
    // Store latest feature for web queries
    {
        std::lock_guard<std::mutex> lock(latest_mutex_);
        latest_feature_ = f;
    }
    bool is_game = is_gaming_scene(f);
    
    float actual_fps = f.frame_interval_us > 0 ? 
                      1000000.0f / static_cast<float>(f.frame_interval_us) : 60.0f;
    
    int cur_cpu = sched_getcpu();
    
    // ========== 新增: Rate Limiting ==========
    auto now_us = std::chrono::steady_clock::now().time_since_epoch().count() / 1000;
    
    // 根据场景设置限速间隔
    rate_limit_us_ = is_game ? RATE_LIMIT_GAME_US : RATE_LIMIT_DAILY_US;
    
    // 检查是否应该跳过此次调频
    if (last_freq_update_us_ > 0 && 
        (now_us - last_freq_update_us_) < static_cast<int64_t>(rate_limit_us_)) {
        // Rate limiting 生效，跳过此次调频
        if (loop_count_ % 20 == 0) {
            LOGD("Rate limiting: skip freq update (delta=%lu us)", now_us - last_freq_update_us_);
        }
        return;
    }
    
    // Update migration engine with current load
    migrator_.update(cur_cpu, f.cpu_util, f.run_queue_len);
    
    int domain_idx = 0;
    const auto& domains = freq_mgr_.domains();
    for (size_t i = 0; i < domains.size(); ++i) {
        for (int cpu : domains[i].cpus) {
            if (cpu == cur_cpu) {
                domain_idx = static_cast<int>(i);
                break;
            }
        }
    }
    const auto& domain = domains[domain_idx];
    
    // ========== 新增: 增强的场景识别 ==========
    // 使用增强的 Predictor 进行场景识别
    SchedScene current_scene = predictor_.get_current_scene();
    
    // 优化 idle 检测
    bool is_idle = (f.cpu_util < 80 && 
                    f.run_queue_len <= 1 &&
                    f.touch_rate_100ms < 5 &&
                    !is_game &&
                    current_scene == SchedScene::IDLE);
    
    FreqConfig cfg;
    
    if (is_idle) {
        // 深度省电: 降到最低频率
        cfg.target_freq = domain.min_freq;
        cfg.min_freq = domain.min_freq;
        cfg.uclamp_min = 0;
        cfg.uclamp_max = 10;
    } else {
        float target_fps = is_game ? 120.0f : 60.0f;
        
        // 传入场景名称给 PolicyEngine
        const char* scene_name = is_game ? "Game" : 
                                 (current_scene == SchedScene::IO_WAIT ? "IO" : "Daily");
        cfg = engine_.decide(f, target_fps, scene_name);
        
        // ========== 增强的 FAS 计算 ==========
        // 使用场景感知的 FAS delta
        int32_t fas_delta = calculate_fas_delta(f, actual_fps, target_fps);
        int32_t adjusted_freq = static_cast<int32_t>(cfg.target_freq) + fas_delta;
        
        // ========== 新增: IO-Wait Boost ==========
        // 当检测到 IO 密集型任务时，逐步 boost 频率
        if (current_scene == SchedScene::IO_WAIT || io_wait_detected_ > 3) {
            uint32_t io_boost = predictor_.get_io_boost();
            if (io_boost > 0) {
                // IO boost: 增加 10-30% 频率
                adjusted_freq = static_cast<int32_t>(adjusted_freq * (1.0f + io_boost / 1024.0f * 0.3f));
            }
        }
        
        // ========== 新增: 触摸加速 ==========
        // 触摸时立即 boost
        if (f.touch_rate_100ms > 20) {
            uint32_t touch_boost = std::min(f.touch_rate_100ms * 2000, 200000u);
            adjusted_freq = std::min(adjusted_freq + touch_boost, 
                                     static_cast<int32_t>(domain.max_freq));
            engine_.on_frame_end();  // 触发帧保持
        }
        
        // 温控缩放 (日常场景更敏感)
        if (f.thermal_margin < 8) {
            adjusted_freq = static_cast<int32_t>(adjusted_freq * 0.82f);
        } else if (f.thermal_margin < 15) {
            adjusted_freq = static_cast<int32_t>(adjusted_freq * 0.90f);
        }
        
        cfg.target_freq = freq_mgr_.snap(static_cast<uint32_t>(adjusted_freq), domain_idx);
        cfg.target_freq = std::clamp(cfg.target_freq, domain.min_freq, domain.max_freq);
        
        // ========== 新增: 帧渲染感知 ==========
        // 当 FPS 低于目标时，额外 boost
        if (actual_fps < target_fps * 0.9f && is_game) {
            cfg.target_freq = std::min(
                static_cast<uint32_t>(cfg.target_freq * 1.1f),
                domain.max_freq
            );
        }
        
        // 日常场景降低 min_freq 约束
        float min_ratio = is_game ? 0.75f : 0.60f;
        cfg.min_freq = std::clamp(
            static_cast<uint32_t>(cfg.target_freq * min_ratio),
            domain.min_freq, 
            cfg.target_freq
        );
        
        // 优化 uclamp (日常场景限制上限)
        float util_norm = static_cast<float>(f.cpu_util) / 1024.0f;
        cfg.uclamp_min = static_cast<uint8_t>(util_norm * 100.0f);
        cfg.uclamp_max = is_game ? 100 : 95;
    }
    
    apply_freq_config(cfg, domain);
    
    // ========== 新增: 更新 Rate Limiting 时间戳 ==========
    last_freq_update_us_ = now_us;
    
    // 线程迁移逻辑 - 日常场景更保守
    if (loop_count_ % 5 == 0) {
        auto mig_result = migrator_.decide(cur_cpu, static_cast<uint32_t>(f.thermal_margin), is_game);
        if (mig_result.go && mig_result.target != cur_cpu) {
            cpu_set_t mask;
            CPU_ZERO(&mask);
            CPU_SET(mig_result.target, &mask);
            
            // 日常场景: 允许更多核心灵活性
            if (!is_game) {
                // 允许调度到相邻核心
                if (mig_result.target > 0) CPU_SET(mig_result.target - 1, &mask);
                if (mig_result.target < static_cast<int>(topo_.get_total_cpus()) - 1) {
                    CPU_SET(mig_result.target + 1, &mask);
                }
            }
            
            // 温控紧急情况
            if (mig_result.thermal) {
                CPU_ZERO(&mask);
                CPU_SET(mig_result.target, &mask);
            }
            
            if (sched_setaffinity(0, sizeof(mask), &mask) == 0) {
                LOGD("Migrate: CPU%d→%d | Util=%u | Therm=%d",
                     cur_cpu, mig_result.target, f.cpu_util, f.thermal_margin);
            }
        }
    }
    
    if (loop_count_ % 20 == 0) {
        // ========== 增强的日志输出 ==========
        const char* scene_names[] = {"IDLE", "LIGHT", "MEDIUM", "HEAVY", "BOOST", "IOWAIT"};
        LOGI("[HyperPredict] Freq=%u | FPS=%.1f | Scene=%s | IOBoost=%u | RateLimit=%u us",
             cfg.target_freq, actual_fps, 
             scene_names[static_cast<int>(current_scene)],
             predictor_.get_io_boost(),
             rate_limit_us_);
    }
}

void EventLoop::adjust(bool increase) noexcept {
    if (increase) {
        period_ms_ = std::max(20u, period_ms_ - 10);
    } else {
        idle_count_++;
        if (idle_count_ > 100) {
            period_ms_ = std::min(200u, period_ms_ + 5);
        }
    }
    
    if (timer_fd_ >= 0) {
        struct itimerspec its{};
        its.it_value.tv_nsec = period_ms_ * 1000000;
        its.it_interval.tv_nsec = period_ms_ * 1000000;
        timerfd_settime(timer_fd_, 0, &its, nullptr);
    }
}

void EventLoop::cleanup() noexcept {
    web_server_.stop();
    
    if (timer_fd_ >= 0) {
        close(timer_fd_);
        timer_fd_ = -1;
    }
    if (epfd_ >= 0) {
        close(epfd_);
        epfd_ = -1;
    }
    // 关闭缓存的 sysfs fd
    for (auto& fc : freq_fds_) {
        if (fc.min_freq_fd >= 0) close(fc.min_freq_fd);
        if (fc.max_freq_fd >= 0) close(fc.max_freq_fd);
        if (fc.uclamp_min_fd >= 0) close(fc.uclamp_min_fd);
        if (fc.uclamp_max_fd >= 0) close(fc.uclamp_max_fd);
    }
}

// WebServerDelegate implementations

net::StatusUpdate EventLoop::get_status() {
    net::StatusUpdate status;
    
    LoadFeature f;
    {
        std::lock_guard<std::mutex> lock(latest_mutex_);
        f = latest_feature_;
    }
    
    status.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    status.fps = f.frame_interval_us > 0 ? 1000000 / f.frame_interval_us : 60;
    status.target_fps = (current_mode_ == 2) ? 120 : (current_mode_ == 1) ? 90 : 60;
    status.cpu_util = f.cpu_util;
    status.run_queue_len = f.run_queue_len;
    status.wakeups_100ms = f.wakeups_100ms;
    status.frame_interval_us = f.frame_interval_us;
    status.touch_rate_100ms = f.touch_rate_100ms;
    status.thermal_margin = f.thermal_margin;
    status.temperature = 42;  // Placeholder
    status.battery_level = f.battery_level;
    status.is_gaming = f.is_gaming;
    status.mode = (current_mode_ == 2) ? "turbo" : (current_mode_ == 1) ? "game" : "daily";
    status.uclamp_min = uclamp_min_.load();
    status.uclamp_max = uclamp_max_.load();
    
    return status;
}

net::ModelWeights EventLoop::get_model_weights() {
    net::ModelWeights weights;
    
    // 获取线性回归权重
    predictor_.export_linear(weights.w_util, weights.w_rq, weights.bias, weights.ema_error);
    
    // 简化: 使用固定权重 (实际可从特征提取器获取)
    weights.w_wakeups = 0.05f;
    weights.w_frame = 0.2f;
    weights.w_touch = 0.02f;
    weights.w_thermal = 0.1f;
    weights.w_battery = 0.01f;
    
    // 获取神经网络状态
    weights.has_nn = (predictor_.get_model() == predict::Predictor::Model::NEURAL);
    
    // 导出神经网络权重
    float nn_weights[32 + 4];  // wh(32) + wo(4)
    float nn_biases[4 + 1];     // bh(4) + bo(1)
    predictor_.export_model(nn_weights, nn_biases);
    
    weights.nn_weights = std::vector<std::vector<std::vector<float>>>(2);
    // 层1: 4×8
    weights.nn_weights[0].resize(4);
    for (size_t h = 0; h < 4; h++) {
        weights.nn_weights[0][h].resize(8);
        for (size_t i = 0; i < 8; i++) {
            weights.nn_weights[0][h][i] = nn_weights[h * 8 + i];
        }
    }
    // 层2: 1×4
    weights.nn_weights[1].resize(1);
    weights.nn_weights[1][0].resize(4);
    for (size_t h = 0; h < 4; h++) {
        weights.nn_weights[1][0][h] = nn_weights[32 + h];
    }
    
    weights.nn_biases.resize(2);
    weights.nn_biases[0] = std::vector<float>(nn_biases, nn_biases + 4);
    weights.nn_biases[1] = std::vector<float>(1, nn_biases[4]);
    
    return weights;
}

bool EventLoop::set_model_weights(const net::ModelWeights& w) {
    // 设置线性回归权重
    predictor_.import_linear(w.w_util, w.w_rq, w.bias, w.ema_error);
    
    // 设置神经网络权重
    if (w.nn_weights.size() >= 2 && w.nn_biases.size() >= 2) {
        predictor_.import_model(w.nn_weights, w.nn_biases);
    }
    
    // 设置激活的模型
    if (w.has_nn) {
        predictor_.set_model(predict::Predictor::Model::NEURAL);
    } else {
        predictor_.set_model(predict::Predictor::Model::LINEAR);
    }
    
    LOGI("Model weights updated: %s", w.has_nn ? "NEURAL" : "LINEAR");
    return true;
}

bool EventLoop::handle_command(const net::WebCommand& cmd) {
    LOGI("Web command: %s", cmd.cmd.c_str());
    
    if (cmd.cmd == "set_mode") {
        std::string mode;
        if (cmd.get_string("mode", mode)) {
            if (mode == "daily") {
                current_mode_ = 0;
                LOGI("Mode set to daily");
            } else if (mode == "game") {
                current_mode_ = 1;
                LOGI("Mode set to game");
            } else if (mode == "turbo") {
                current_mode_ = 2;
                LOGI("Mode set to turbo");
            }
            return true;
        }
    } else if (cmd.cmd == "set_uclamp") {
        int min_val, max_val;
        if (cmd.get_int("min", min_val)) {
            uclamp_min_ = static_cast<uint8_t>(std::clamp(min_val, 0, 100));
        }
        if (cmd.get_int("max", max_val)) {
            uclamp_max_ = static_cast<uint8_t>(std::clamp(max_val, 0, 100));
        }
        LOGI("uclamp set: %u-%u", uclamp_min_.load(), uclamp_max_.load());
        return true;
    } else if (cmd.cmd == "set_thermal") {
        std::string preset;
        if (cmd.get_string("preset", preset)) {
            thermal_preset_ = preset;
            LOGI("Thermal preset set: %s", preset.c_str());
            return true;
        }
    } else if (cmd.cmd == "set_model") {
        std::string model;
        if (cmd.get_string("model", model)) {
            if (model == "linear") {
                predictor_.set_model(predict::Predictor::Model::LINEAR);
                LOGI("Predictor model: LINEAR");
            } else if (model == "neural") {
                predictor_.set_model(predict::Predictor::Model::NEURAL);
                LOGI("Predictor model: NEURAL");
            }
            return true;
        }
    }
    
    return false;
}

void EventLoop::save() noexcept {
    LOGI("Saving state...");
}

void EventLoop::start() noexcept {
    if (!init()) {
        LOGE("Initialization failed, exiting");
        return;
    }
    
    running_ = true;
        uint32_t fc = 0;
    const uint32_t ci = (period_ms_ > 100) ? 20 : 5;
    
    LOGI("=== Event Loop Started ===");
    
    while (running_.load(std::memory_order_relaxed)) {
        loop_count_++;
        
        struct epoll_event ev[4];
        int n = epoll_wait(epfd_, ev, 4, period_ms_);
        
        if (n < 0) {
            if (errno == EINTR) {
                usleep(period_ms_ * 1000);
                continue;
            }
            LOGE("epoll_wait error: %s", strerror(errno));
            break;
        }
        
        if (n == 0) {
            idle_count_++;
            if (idle_count_ > 50) {
                adjust(false);
            }
            continue;
        }
        
        idle_count_ = 0;
        
        for (int i = 0; i < n; ++i) {
            if (ev[i].data.fd == timer_fd_) {
                uint64_t buf;
                ssize_t bytes = read(timer_fd_, &buf, 8);
                if (bytes == 8) {
                    process();
                }
            }
        }
        
        if (++fc >= ci) {
            collect();
            fc = 0;
        }
    }
    
    LOGI("=== Event Loop Stopped ===");
    save();
    cleanup();
}

bool EventLoop::detect_sched_backend() noexcept {
    // 1. 检测 uclamp (cgroup v1)
    if (access("/dev/cpuctl/cpu0/uclamp.min", F_OK) == 0) {
        sched_backend_ = SchedBackend::UCLAMP;
        LOGI("Sched Backend: UCLAMP (cgroup v1)");
        return true;
    }
    
    // 2. 检测 cgroup v2
    if (access("/sys/fs/cgroup/cgroup.subtree_control", F_OK) == 0) {
        sched_backend_ = SchedBackend::CGROUP_V2;
        LOGI("Sched Backend: CGROUP_V2");
        return true;
    }
    
    // 3. 检测 cgroup v1 cpu
    if (access("/sys/fs/cgroup/cpu", F_OK) == 0) {
        sched_backend_ = SchedBackend::CGROUP_V2;
        LOGI("Sched Backend: CGROUP_V1 (legacy)");
        return true;
    }
    
    // 4. 仅频率控制 (无 cgroup 支持)
    sched_backend_ = SchedBackend::FREQ_ONLY;
    LOGW("Sched Backend: FREQ_ONLY (no cgroup support)");
    LOGW("UCLAMP not available - using frequency compensation");
    
    return false;
}

uint32_t EventLoop::get_compensated_freq(uint32_t base_freq, uint8_t uclamp_min) const noexcept {
    // 当 uclamp 不可用时，通过提高 CPU 频率来补偿
    // uclamp_min 范围 0-100，转换为补偿系数
    
    if (uclamp_min == 0) {
        // 最低优先级，使用最低频率
        return freq_mgr_.domains().empty() ? base_freq : freq_mgr_.domains()[0].min_freq;
    }
    
    if (uclamp_min >= 100) {
        // 最高优先级，全速运行
        return freq_mgr_.domains().empty() ? base_freq : freq_mgr_.domains()[0].max_freq;
    }
    
    // 线性插值: uclamp_min 0% -> 最低频率, uclamp_min 100% -> 最高频率
    if (freq_mgr_.domains().empty()) {
        return base_freq;
    }
    
    const auto& domain = freq_mgr_.domains()[0];
    uint32_t freq_range = domain.max_freq - domain.min_freq;
    uint32_t compensated = domain.min_freq + (freq_range * uclamp_min / 100);
    
    // 加上基准频率的 50% 作为基础
    compensated = std::max(compensated, base_freq * 50 / 100);
    
    return std::clamp(compensated, domain.min_freq, domain.max_freq);
}

} // namespace hp