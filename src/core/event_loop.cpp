#include "core/event_loop.h"
#include "core/logger.h"
#include <cstring>
#include <cstdlib>
#include <sched.h>
#include <cerrno>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <fcntl.h>

namespace hp {

void EventLoop::setup_epoll() noexcept {
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if(epfd_ < 0) {
        LOGE("epoll_create1 failed: %s", strerror(errno));
        return;
    }
}

void EventLoop::setup_timer() noexcept {
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if(timer_fd_ < 0) {
        LOGE("timerfd_create failed: %s", strerror(errno));
        return;
    }
    
    struct itimerspec its{};
    its.it_value.tv_nsec = current_period_ms_ * 1000000;
    its.it_interval.tv_nsec = current_period_ms_ * 1000000;
    
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
}

void EventLoop::setup_thermal_monitor() noexcept {
    thermal_fd_ = open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY);
    if(thermal_fd_ >= 0) {        struct epoll_event ev{};
        ev.events = EPOLLPRI;
        ev.data.fd = thermal_fd_;
        epoll_ctl(epfd_, EPOLL_CTL_ADD, thermal_fd_, &ev);
        LOGD("Thermal monitor added");
    }
}

void EventLoop::collect_features() noexcept {
    LoadFeature f = collector_.collect();
    f = extractor_.extract(
        f.cpu_util, f.run_queue_len, f.wakeups_100ms,
        f.frame_interval_us, f.touch_rate_100ms,
        f.thermal_margin, f.battery_level
    );
    
    if(q_.try_push(f)) {
        if(f.is_gaming && f.frame_interval_us > 16000) {
            LOGD("Game frame drop: util=%u frame=%uus", f.cpu_util, f.frame_interval_us);
        }
    }
}

void EventLoop::adjust_period(bool urgent) noexcept {
    if(urgent) {
        current_period_ms_ = std::max(MIN_PERIOD_MS, current_period_ms_ - 10);
    } else {
        idle_counter_++;
        if(idle_counter_ > 100) {
            current_period_ms_ = std::min(MAX_PERIOD_MS, current_period_ms_ + 5);
        }
    }
    
    if(timer_fd_ >= 0) {
        struct itimerspec its{};
        its.it_value.tv_nsec = current_period_ms_ * 1000000;
        its.it_interval.tv_nsec = current_period_ms_ * 1000000;
        timerfd_settime(timer_fd_, 0, &its, nullptr);
    }
}

void EventLoop::process_decisions(bool urgent) noexcept {
    const auto& big_cores = topology_.get_big_cores();
    if(big_cores.empty()) return;

    if(auto f = q_.try_pop()) {
        float temp_factor = 1.0f;
        if(f->thermal_margin < 10) temp_factor = 0.80f;
        else if(f->thermal_margin < 15) temp_factor = 0.90f;
        else if(f->thermal_margin < 25) temp_factor = 0.95f;        
        const char* pkg = "com.target.game";
        float sim_fps = 54.f + (f->boost_prob / 100.f) * 6.f;
        FreqConfig cfg = engine_.decide(*f, sim_fps, pkg);
        
        cfg.target_freq = static_cast<uint32_t>(static_cast<float>(cfg.target_freq) * temp_factor);
        cfg.min_freq = static_cast<uint32_t>(static_cast<float>(cfg.min_freq) * temp_factor);
        
        if(f->is_gaming) {
            if(f->boost_prob > 70 && f->frame_interval_us > 14000) {
                cfg.target_freq = std::min(3000000u, cfg.target_freq + 200000u);
                if(writer_.uclamp_supported()) {
                    uint8_t new_clamp = cfg.uclamp_max + 15;
                    cfg.uclamp_max = (new_clamp > 100) ? 100 : new_clamp;
                }
                adjust_period(true);
            }
            if(f->frame_interval_us > 18000) {
                cfg.target_freq = std::min(3000000u, cfg.target_freq + 300000u);
                if(writer_.uclamp_supported()) {
                    cfg.uclamp_max = 100;
                }
                adjust_period(true);
            }
        }
        
        std::vector<std::pair<int, FreqConfig>> batch;
        for(int cpu : big_cores) {
            batch.emplace_back(cpu, cfg);
        }
        
        if(writer_.set_batch(batch)) {
            if(writer_.uclamp_supported()) {
                LOGI("Freq: %u kHz | uclamp: %u-%u | game=%d period=%dms", 
                     cfg.target_freq, cfg.uclamp_min, cfg.uclamp_max, 
                     f->is_gaming?1:0, current_period_ms_);
            } else if(writer_.cgroups_supported()) {
                uint16_t shares = (cfg.uclamp_max * 1024) / 100;
                LOGI("Freq: %u kHz | shares: %u | game=%d period=%dms", 
                     cfg.target_freq, shares, f->is_gaming?1:0, current_period_ms_);
            } else {
                LOGI("Freq: %u kHz (freq only) | game=%d period=%dms", 
                     cfg.target_freq, f->is_gaming?1:0, current_period_ms_);
            }
        }
    }
}
void EventLoop::cleanup() noexcept {
    if(timer_fd_ >= 0) close(timer_fd_);
    if(epfd_ >= 0) close(epfd_);
    if(thermal_fd_ >= 0) close(thermal_fd_);
}

void EventLoop::save_model() noexcept {
    if(engine_.export_model(MODEL_BIN_PATH)) {
        LOGI("Model saved: %s", MODEL_BIN_PATH);
    }
    if(engine_.export_model_json(MODEL_JSON_PATH)) {
        LOGI("JSON exported: %s", MODEL_JSON_PATH);
    }
}

void EventLoop::export_model_json() noexcept {
    if(engine_.export_model_json(MODEL_JSON_PATH)) {
        LOGI("JSON exported: %s", MODEL_JSON_PATH);
    } else {
        LOGE("Failed to export JSON");
    }
}

void EventLoop::start() {
    LOGI("=== HyperPredict v2.2 (Adaptive) ===");
    
    if(writer_.uclamp_supported()) {
        LOGI("Control: uclamp");
    } else if(writer_.cgroups_supported()) {
        LOGI("Control: cpu.shares");
    } else {
        LOGW("Control: frequency only");
    }
    
    if(engine_.load_model(MODEL_BIN_PATH)) {
        LOGI("Model loaded");
    } else {
        LOGI("Fresh start");
    }
    
    if(!topology_.detect()) {
        LOGE("Topology detection failed");
        return;
    }

    int target_cpu = 0;
    const auto& big_cores = topology_.get_big_cores();
    if(big_cores.size() >= 2) target_cpu = big_cores[1];
    else if(!big_cores.empty()) target_cpu = big_cores[0];
        cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(target_cpu, &mask);
    if(sched_setaffinity(0, sizeof(mask), &mask) == 0) {
        LOGI("Bound to CPU%d", target_cpu);
    }

    calibrator_.calibrate(topology_);
    engine_.init(calibrator_.baseline());
    LOGI("Baseline: %u kHz", calibrator_.baseline().big.target_freq);

    setup_epoll();
    setup_timer();
    setup_thermal_monitor();
    if(epfd_ < 0 || timer_fd_ < 0) return;

    LOGI("Loop started (adaptive %d-%dms)", MIN_PERIOD_MS, MAX_PERIOD_MS);
    
    struct epoll_event events[MAX_EVENTS];
    uint64_t timer_buf;
    uint32_t feature_counter = 0;
    int loop_count = 0;

    while(run_.load(std::memory_order_relaxed)) {
        loop_count++;
        
        int n = epoll_wait(epfd_, events, MAX_EVENTS, current_period_ms_);
        
        if(n < 0) {
            if(errno == EINTR) continue;
            LOGE("epoll failed: %s", strerror(errno));
            break;
        }

        if(n == 0) {
            idle_counter_++;
            if(idle_counter_ > 50) {
                adjust_period(false);
            }
            if(loop_count % 20 == 0) LOGD("Idle %d period=%dms", loop_count, current_period_ms_);
            continue;
        }

        idle_counter_ = 0;
        bool urgent = false;
        
        for(int i = 0; i < n; ++i) {
            if(events[i].data.fd == timer_fd_) {
                if(read(timer_fd_, &timer_buf, 8) == 8) {
                    process_decisions(urgent);                }
            } else if(events[i].data.fd == thermal_fd_) {
                urgent = true;
                LOGD("Thermal event!");
            }
        }
        
        adjust_period(urgent);

        int collect_interval = (current_period_ms_ > 100) ? 
                               COLLECT_INTERVAL_IDLE : COLLECT_INTERVAL_NORMAL;
        
        if(++feature_counter >= collect_interval) {
            collect_features();
            feature_counter = 0;
        }
    }

    LOGI("Stopped (iterations: %d final_period=%dms)", loop_count, current_period_ms_);
    save_model();
    cleanup();
}

} // namespace hp