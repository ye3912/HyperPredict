#include "core/event_loop.h"
#include "core/logger.h"
#include <cstring>
#include <cstdlib>
#include <sched.h>
#include <cerrno>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

namespace hp {

void EventLoop::setup_epoll() noexcept {
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if(epfd_ < 0) {
        LOGE("epoll_create1 failed: %s", strerror(errno));
        return;
    }
    LOGD("Epoll created: fd=%d", epfd_);
}

void EventLoop::setup_timer() noexcept {
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if(timer_fd_ < 0) {
        LOGE("timerfd_create failed: %s", strerror(errno));
        return;
    }
    
    struct itimerspec its{};
    its.it_value.tv_nsec = DECISION_PERIOD_MS * 1000000;
    its.it_interval.tv_nsec = DECISION_PERIOD_MS * 1000000;
    
    if(timerfd_settime(timer_fd_, 0, &its, nullptr) < 0) {
        LOGE("timerfd_settime failed: %s", strerror(errno));
        return;
    }
    
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd_;
    
    if(epoll_ctl(epfd_, EPOLL_CTL_ADD, timer_fd_, &ev) < 0) {
        LOGE("epoll_ctl add timer failed: %s", strerror(errno));
        return;
    }
    
    LOGD("Timer created: fd=%d, period=%dms", timer_fd_, DECISION_PERIOD_MS);
}

void EventLoop::collect_features() noexcept {    LoadFeature f = collector_.collect();
    f = extractor_.extract(
        f.cpu_util,
        f.run_queue_len,
        f.wakeups_100ms,
        f.frame_interval_us,
        f.touch_rate_100ms,
        f.thermal_margin,
        f.battery_level
    );
    
    if(q_.try_push(f)) {
        if(f.is_gaming) {
            LOGD("Game feature: util=%u frame=%uus temp_margin=%d", 
                 f.cpu_util, f.frame_interval_us, f.thermal_margin);
        }
    }
}

void EventLoop::process_decisions() noexcept {
    const auto& big_cores = topology_.get_big_cores();
    if(big_cores.empty()) {
        LOGW("No big cores found");
        return;
    }

    if(auto f = q_.try_pop()) {
        // 温度补偿因子
        float temp_factor = 1.0f;
        if(f->thermal_margin < 10) {
            temp_factor = 0.80f;  // 高温严重降频
        } else if(f->thermal_margin < 15) {
            temp_factor = 0.90f;  // 高温保守
        } else if(f->thermal_margin < 25) {
            temp_factor = 0.95f;  // 中温略保守
        }
        
        const char* pkg = "com.target.game";
        float sim_fps = 54.f + (f->boost_prob / 100.f) * 6.f;
        FreqConfig cfg = engine_.decide(*f, sim_fps, pkg);
        
        // 应用温度因子
        cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * temp_factor);
        cfg.min_freq = static_cast<uint32_t>(cfg.min_freq * temp_factor);
        
        // 游戏场景激进策略
        if(f->is_gaming) {
            if(f->boost_prob > 70 && f->frame_interval_us > 14000) {
                // 高负载游戏，强制升频
                cfg.target_freq = std::min(2400000u, cfg.target_freq + 200000);                cfg.uclamp_max = std::min(100u, static_cast<uint8_t>(cfg.uclamp_max + 15));
                cfg.uclamp_min = std::min(100u, cfg.uclamp_min + 10);
            }
            
            // 掉帧预防
            if(f->frame_interval_us > 18000) {
                cfg.target_freq = std::min(2600000u, cfg.target_freq + 300000);
                cfg.uclamp_max = 100;
            }
        }
        
        std::vector<std::pair<int, FreqConfig>> batch;
        for(int cpu : big_cores) {
            batch.emplace_back(cpu, cfg);
        }
        
        if(writer_.set_batch(batch)) {
            LOGI("Freq: %u kHz | temp_m=%d | prob=%u | gaming=%d", 
                 cfg.target_freq, f->thermal_margin, f->boost_prob, f->is_gaming ? 1 : 0);
        } else {
            LOGE("Failed to set frequency");
        }
    }
}

void EventLoop::cleanup() noexcept {
    if(timer_fd_ >= 0) {
        close(timer_fd_);
        timer_fd_ = -1;
    }
    if(epfd_ >= 0) {
        close(epfd_);
        epfd_ = -1;
    }
    LOGD("Cleanup complete");
}

void EventLoop::save_model() noexcept {
    if(engine_.export_model(MODEL_BIN_PATH)) {
        LOGI("Binary model saved: %s", MODEL_BIN_PATH);
    }
    
    if(engine_.export_model_json(MODEL_JSON_PATH)) {
        LOGI("JSON model exported: %s", MODEL_JSON_PATH);
    }
}

void EventLoop::start() {
    LOGI("=== Event Loop Start (Game Optimized) ===");
        // 加载旧模型
    LOGI("Loading model from %s", MODEL_BIN_PATH);
    if(engine_.load_model(MODEL_BIN_PATH)) {
        LOGI("Model loaded successfully");
    } else {
        LOGI("No valid model found, starting fresh");
    }
    
    // 检测拓扑
    LOGI("Detecting CPU topology...");
    if(!topology_.detect()) {
        LOGE("Failed to detect CPU topology");
        return;
    }
    LOGI("CPU topology detected");

    // 游戏优化：绑定到次大核
    int target_cpu = 0;
    const auto& big_cores = topology_.get_big_cores();
    if(big_cores.size() >= 2) {
        target_cpu = big_cores[1];  // 次大核
    } else if(!big_cores.empty()) {
        target_cpu = big_cores[0];
    } else if(!topology_.get_little_cores().empty()) {
        target_cpu = topology_.get_little_cores().back();
    }
    
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(target_cpu, &mask);
    if(sched_setaffinity(0, sizeof(mask), &mask) == 0) {
        LOGI("Bound to CPU%d (game optimized)", target_cpu);
    } else {
        LOGW("Failed to bind CPU: %s", strerror(errno));
    }

    // 校准基线
    LOGI("Calibrating baseline...");
    calibrator_.calibrate(topology_);
    engine_.init(calibrator_.baseline());
    LOGI("Baseline ready. Big target: %u kHz", calibrator_.baseline().big.target_freq);

    // 初始化 epoll
    LOGI("Setting up epoll...");
    setup_epoll();
    setup_timer();
    
    if(epfd_ < 0 || timer_fd_ < 0) {
        LOGE("Failed to setup epoll/timer");
        return;    }
    LOGI("Epoll setup complete");

    // 主循环
    LOGI("Entering main loop (period=%dms, collect=%d)...", 
         DECISION_PERIOD_MS, COLLECT_INTERVAL);
    
    struct epoll_event events[MAX_EVENTS];
    uint64_t timer_buf;
    uint32_t feature_counter = 0;
    int loop_count = 0;

    while(run_.load(std::memory_order_relaxed)) {
        loop_count++;
        
        int n = epoll_wait(epfd_, events, MAX_EVENTS, DECISION_PERIOD_MS);
        
        if(n < 0) {
            if(errno == EINTR) {
                continue;
            }
            LOGE("epoll_wait failed: %s", strerror(errno));
            break;
        }

        if(n == 0) {
            if(loop_count % 20 == 0) {
                LOGD("Idle (iteration %d)", loop_count);
            }
            continue;
        }

        for(int i = 0; i < n; ++i) {
            if(events[i].data.fd == timer_fd_) {
                ssize_t r = read(timer_fd_, &timer_buf, 8);
                if(r == 8) {
                    process_decisions();
                }
            }
        }

        if(++feature_counter >= COLLECT_INTERVAL) {
            collect_features();
            feature_counter = 0;
        }
    