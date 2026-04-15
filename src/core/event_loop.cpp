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
}

void EventLoop::collect_features() noexcept {
    LoadFeature f = collector_.collect();
    f = extractor_.extract(
        f.cpu_util, f.run_queue_len, f.wakeups_100ms,        f.frame_interval_us, f.touch_rate_100ms,
        f.thermal_margin, f.battery_level
    );
    
    if(q_.try_push(f)) {
        if(f.is_gaming) {
            LOGD("Game: util=%u frame=%uus temp_m=%d", 
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
            }
            if(f->frame_interval_us > 18000) {
                cfg.target_freq = std::min(3000000u, cfg.target_freq + 300000u);
                if(writer_.uclamp_supported()) {
                    cfg.uclamp_max = 100;
                }
            }
        }
        
        std::vector<std::pair<int, FreqConfig>> batch;
        for(int cpu : big_cores) {            batch.emplace_back(cpu, cfg);
        }
        
        if(writer_.set_batch(batch)) {
            if(writer_.uclamp_supported()) {
                LOGI("Freq: %u kHz | uclamp: %u-%u | game=%d", 
                     cfg.target_freq, cfg.uclamp_min, cfg.uclamp_max, f->is_gaming?1:0);
            } else if(writer_.cgroups_supported()) {
                uint16_t shares = (cfg.uclamp_max * 1024) / 100;
                LOGI("Freq: %u kHz | shares: %u | game=%d", 
                     cfg.target_freq, shares, f->is_gaming?1:0);
            } else {
                LOGI("Freq: %u kHz (freq only) | game=%d", 
                     cfg.target_freq, f->is_gaming?1:0);
            }
        } else {
            LOGE("Failed to set frequency");
        }
    }
}

void EventLoop::cleanup() noexcept {
    if(timer_fd_ >= 0) close(timer_fd_);
    if(epfd_ >= 0) close(epfd_);
}

void EventLoop::save_model() noexcept {
    if(engine_.export_model(MODEL_BIN_PATH)) {
        LOGI("Model saved: %s", MODEL_BIN_PATH);
    }
    if(engine_.export_model_json(MODEL_JSON_PATH)) {
        LOGI("JSON exported: %s", MODEL_JSON_PATH);
    }
}

void EventLoop::start() {
    LOGI("=== HyperPredict v2.1 ===");
    
    if(writer_.uclamp_supported()) {
        LOGI("Control: uclamp (full precision)");
    } else if(writer_.cgroups_supported()) {
        LOGI("Control: cpu.shares (cgroups fallback)");
    } else {
        LOGW("Control: frequency only (no uclamp/cgroups)");
    }
    
    if(engine_.load_model(MODEL_BIN_PATH)) {
        LOGI("Model loaded");
    } else {
        LOGI("Fresh start");    }
    
    if(!topology_.detect()) {
        LOGE("Topology detection failed");
        return;
    }

    int target_cpu = 0;
    const auto& big_cores = topology_.get_big_cores();
    