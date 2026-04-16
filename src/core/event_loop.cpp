#include "core/event_loop.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sched.h>
#include <algorithm>

namespace hp {

// ────────── 构造函数 ──────────
EventLoop::EventLoop()
    : epfd_(-1)
    , timer_fd_(-1)
    , period_ms_(100)
    , loop_count_(0)
    , idle_count_(0)
    , running_(false) {
}

// ────────── 初始化 ──────────
bool EventLoop::init() noexcept {
    LOGI("=== HyperPredict v4.2 Initializing ===");
    
    // 1. 硬件分析
    if (!hw_.analyze()) {
        LOGE("Hardware analysis failed");
        return false;
    }
    
    // 2. 拓扑检测
    if (!topo_.detect()) {
        LOGE("Topology detection failed");
        return false;
    }
    
    // 3. 频率管理器
    if (!freq_mgr_.init()) {
        LOGE("FreqManager init failed");
        return false;
    }
    
    // 4. 迁移引擎
    migrator_.init(hw_.profile());
        // 5. 绑核器
    binder_.init(hw_.profile());
    binder_.bind_sched();
    
    // 6. 校准基线
    calibrator_.calibrate(topo_, freq_mgr_);
    
    // 7. 调度引擎
    engine_.init(calibrator_.baseline());
    
    // 8. 预测器
    predictor_.train(hw_.profile());
    
    // 9. epoll 初始化
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) {
        LOGE("epoll_create1 failed: %s", strerror(errno));
        return false;
    }
    
    // 10. 定时器初始化
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
        return false;    }
    
    LOGI("Initialization complete | Period=%ums | Cores=%d", 
         period_ms_, topo_.get_total_cpus());
    
    return true;
}

// ────────── 数据采集 ──────────
void EventLoop::collect() noexcept {
    LoadFeature f = collector_.collect();
    
    if (!queue_.try_push(f)) {
        LOGW("Queue full, dropping frame");
    }
}

// ────────── 游戏场景检测 ──────────
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
// ────────── FAS 增量计算 ──────────
int32_t EventLoop::calculate_fas_delta(const LoadFeature& f, float current_fps, 
                                        float target_fps) noexcept {
    static int32_t last_delta = 0;
    
    float fps_error = target_fps - current_fps;
    
    // 计算基础修正
    int32_t delta = static_cast<int32_t>(fps_error * 10000.0f);
    
    // 平滑处理
    delta = static_cast<int32_t>(last_delta * 0.7f + delta * 0.3f);
    
    // 限制范围
    delta = std::clamp(delta, -300000, 300000);
    
    // 死区过滤
    if (std::abs(delta) < 50000) {
        delta = 0;
    }
    
    last_delta = delta;
    return delta;
}

// ────────── 应用频率配置 ──────────
void EventLoop::apply_freq_config(const FreqConfig& cfg, 
                                   const device::FreqDomain& domain) noexcept {
    for (int cpu : domain.cpus) {
        char path[128];
        
        // 设置 min_freq
        snprintf(path, sizeof(path), 
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
        FILE* fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "%u", cfg.min_freq);
            fclose(fp);
        }
        
        // 设置 max_freq
        snprintf(path, sizeof(path), 
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "%u", cfg.target_freq);
            fclose(fp);
        }
        
        // uclamp (如果支持)        snprintf(path, sizeof(path), 
                 "/dev/cpuctl/cpu%d/uclamp.min", cpu);
        fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "%u", cfg.uclamp_min);
            fclose(fp);
        }
        
        snprintf(path, sizeof(path), 
                 "/dev/cpuctl/cpu%d/uclamp.max", cpu);
        fp = fopen(path, "w");
        if (fp) {
            fprintf(fp, "%u", cfg.uclamp_max);
            fclose(fp);
        }
    }
}

// ────────── 主处理逻辑 ──────────
void EventLoop::process() noexcept {
    auto f_opt = queue_.try_pop();
    if (!f_opt) return;
    
    const LoadFeature& f = *f_opt;
    bool is_game = is_gaming_scene(f);
    
    // 获取当前 CPU
    int cur_cpu = sched_getcpu();
    int domain_idx = freq_mgr_.get_domain_index(cur_cpu);
    if (domain_idx < 0) domain_idx = 0;
    
    const auto& domain = freq_mgr_.domains()[domain_idx];
    
    // 空闲检测
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
        float current_fps = 1000000.0f / static_cast<float>(f.frame_interval_us);
        
        cfg = engine_.decide(f, target_fps, is_game ? "Game" : "Daily");        
        // FAS 修正
        int32_t fas_delta = calculate_fas_delta(f, current_fps, target_fps);
        
        // 应用 FAS
        int32_t adjusted_freq = static_cast<int32_t>(cfg.target_freq) + fas_delta;
        
        // 温控缩放
        if (f.thermal_margin < 5) {
            adjusted_freq = static_cast<int32_t>(adjusted_freq * 0.85f);
        } else if (f.thermal_margin < 10) {
            adjusted_freq = static_cast<int32_t>(adjusted_freq * 0.92f);
        }
        
        // 频率对齐
        cfg.target_freq = freq_mgr_.snap(static_cast<uint32_t>(adjusted_freq), domain_idx);
        cfg.target_freq = std::clamp(cfg.target_freq, domain.min_freq, domain.max_freq);
        cfg.min_freq = std::clamp(static_cast<uint32_t>(cfg.target_freq * 75 / 100), 
                                   domain.min_freq, cfg.target_freq);
        
        // uclamp 设置
        float util_norm = static_cast<float>(f.cpu_util) / 1024.0f;
        cfg.uclamp_min = static_cast<uint8_t>(util_norm * 100.0f);
        cfg.uclamp_max = is_game ? 100 : std::min(100, static_cast<int>(cfg.uclamp_min + 20));
    }
    
    // 应用频率配置
    apply_freq_config(cfg, domain);
    
    // 核间迁移决策
    if (loop_count_ % 5 == 0) {
        auto mig_result = migrator_.decide(cur_cpu, static_cast<uint32_t>(f.thermal_margin), is_game);
        if (mig_result.go) {
            cpu_set_t mask;
            CPU_ZERO(&mask);
            CPU_SET(mig_result.target, &mask);
            
            if (sched_setaffinity(0, sizeof(mask), &mask) == 0) {
                LOGD("Migrate: CPU%d→%d | Util=%u | Therm=%d", 
                     cur_cpu, mig_result.target, f.cpu_util, f.thermal_margin);
            }
        }
    }
    
    // 日志输出 (每 20 次循环)
    if (loop_count_ % 20 == 0) {
        LOGI("Freq=%u | Idle=%d | Game=%d", cfg.target_freq, is_idle ? 1 : 0, is_game ? 1 : 0);
    }
}
// ────────── 动态调整周期 ──────────
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

// ────────── 清理资源 ──────────
void EventLoop::cleanup() noexcept {
    if (timer_fd_ >= 0) {
        close(timer_fd_);
        timer_fd_ = -1;
    }
    if (epfd_ >= 0) {
        close(epfd_);
        epfd_ = -1;
    }
}

// ────────── 保存状态 ──────────
void EventLoop::save() noexcept {
    LOGI("Saving state...");
}

// ────────── 主循环 ──────────
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

} // namespace hp