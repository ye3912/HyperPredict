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

namespace hp {

EventLoop::EventLoop()
    : epfd_(-1)
    , timer_fd_(-1)
    , period_ms_(100)
    , loop_count_(0)
    , idle_count_(0)
    , running_(false)
    , web_server_(net::DEFAULT_PORT) {
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
        if (!queue_.try_push(f)) {
        LOGW("Queue full, dropping frame");
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
    
    float actual_fps = 1000000.0f / static_cast<float>(f.frame_interval_us);
    
    int cur_cpu = sched_getcpu();
    
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
    
    bool is_idle = (f.cpu_util < 100 && 
                    f.frame_interval_us > 33333 && 
                    !is_game);
    
    FreqConfig cfg;
    
    if (is_idle) {
        cfg.target_freq = domain.min_freq;
        cfg.min_freq = domain.min_freq;
        cfg.uclamp_min = 0;
        cfg.uclamp_max = 10;
    } else {
        float target_fps = is_game ? 120.0f : 60.0f;
        
        cfg = engine_.decide(f, target_fps, is_game ? "Game" : "Daily");
        
        int32_t fas_delta = calculate_fas_delta(f, actual_fps, target_fps);
        int32_t adjusted_freq = static_cast<int32_t>(cfg.target_freq) + fas_delta;
        
        if (f.thermal_margin < 5) {
            adjusted_freq = static_cast<int32_t>(adjusted_freq * 0.85f);
        } else if (f.thermal_margin < 10) {
            adjusted_freq = static_cast<int32_t>(adjusted_freq * 0.92f);
        }
        
        cfg.target_freq = freq_mgr_.snap(static_cast<uint32_t>(adjusted_freq), domain_idx);
        cfg.target_freq = std::clamp(cfg.target_freq, domain.min_freq, domain.max_freq);
        cfg.min_freq = std::clamp(static_cast<uint32_t>(cfg.target_freq * 75 / 100), 
                                   domain.min_freq, cfg.target_freq);
        
        float util_norm = static_cast<float>(f.cpu_util) / 1024.0f;
        cfg.uclamp_min = static_cast<uint8_t>(util_norm * 100.0f);
        cfg.uclamp_max = is_game ? 100 : std::min(100, static_cast<int>(cfg.uclamp_min + 20));
    }
    
    apply_freq_config(cfg, domain);
    
    // 线程迁移逻辑 - 使用软亲和性偏好
    if (loop_count_ % 5 == 0) {
        auto mig_result = migrator_.decide(cur_cpu, static_cast<uint32_t>(f.thermal_margin), is_game);
        if (mig_result.go && mig_result.target != cur_cpu) {
            // 构建亲和性掩码：目标核心 + 相邻核心（允许调度器灵活分配）
            cpu_set_t mask;
            CPU_ZERO(&mask);
            CPU_SET(mig_result.target, &mask);
            
            // 允许调度到相邻核心，增加灵活性
            if (mig_result.target > 0) CPU_SET(mig_result.target - 1, &mask);
            if (mig_result.target < (int)topo_.get_total_cpus() - 1) CPU_SET(mig_result.target + 1, &mask);
            
            // 温控紧急情况：强制绑定到指定核心
            if (mig_result.thermal) {
                CPU_ZERO(&mask);
                CPU_SET(mig_result.target, &mask);
            }
            
            if (sched_setaffinity(0, sizeof(mask), &mask) == 0) {
                LOGD("Migrate: CPU%d→%d | Util=%u | Therm=%d | Thermal=%d",
                     cur_cpu, mig_result.target, f.cpu_util, f.thermal_margin, mig_result.thermal ? 1 : 0);
            }
        }
    }
    
    if (loop_count_ % 20 == 0) {
        LOGI("Freq=%u | FPS=%.1f | Idle=%d | Game=%d", 
             cfg.target_freq, actual_fps, is_idle ? 1 : 0, is_game ? 1 : 0);
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
    
    // Get predictor weights
    // Note: In real implementation, expose predictor_'s internal weights
    weights.w_util = 0.3f;
    weights.w_rq = -0.1f;
    weights.w_wakeups = 0.05f;
    weights.w_frame = 0.2f;
    weights.w_touch = 0.02f;
    weights.w_thermal = 0.1f;
    weights.w_battery = 0.01f;
    weights.bias = 55.0f;
    weights.ema_error = 2.5f;
    weights.has_nn = false;
    
    return weights;
}

bool EventLoop::set_model_weights(const net::ModelWeights& weights) {
    // Set predictor weights
    // Note: In real implementation, update predictor_'s internal weights
    LOGI("Model weights update requested");
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