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
    if(big_cores.empty()) return;

    if(auto f = q_.try_pop()) {
        float temp_factor = 1.0f;
        if(f->thermal_margin < 10) temp_factor = 0.80f;
        else if(f->thermal_margin < 15) temp_factor = 0.90f;
        else if(f->thermal_margin < 25) temp_factor = 0.95f;
        
        const char* pkg = "com.target.game";
        float sim_fps = 54.f + (f->boost_prob / 100.f) * 6.f;
        FreqConfig cfg = engine_.decide(*f, sim_fps, pkg);
        
        cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * temp_factor);
        cfg.min_freq = static_cast<uint32_t>(cfg.min_freq * temp_factor);
        
        if(f->is_gaming) {
            if(f->boost_prob > 70 && f->frame_interval_us > 14000) {
                cfg.target_freq = std::min(2400000u, cfg.target_freq + 200000);
                cfg.uclamp_max = std::min(100u, static_cast<uint8_t>(cfg.uclamp_max + 15));
            }
            if(f->frame_interval_us > 18000) {
                cfg.target_freq = std::min(2600000u, cfg.target_freq + 300000);
                cfg.uclamp_max = 100;
            }
        }
        
        std::vector<std::pair<int, FreqConfig>> batch;
        for(int cpu : big_cores) batch.emplace_back(cpu, cfg);
        
        if(writer_.set_batch(batch)) {
            LOGI("Freq: %u kHz | temp_m=%d | prob=%u | game=%d", 
                 cfg.target_freq, f->thermal_margin, f->boost_prob, f->is_gaming?1:0);
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
    LOGI("=== HyperPredict v2.1 (Game+JSON+Perf) ===");
    
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
    if(epfd_ < 0 || timer_fd_ < 0) return;

    LOGI("Loop started (period=%dms)", DECISION_PERIOD_MS);
        struct epoll_event events[MAX_EVENTS];
    uint64_t timer_buf;
    uint32_t feature_counter = 0;
    int loop_count = 0;

    while(run_.load(std::memory_order_relaxed)) {
        loop_count++;
        
        int n = epoll_wait(epfd_, events, MAX_EVENTS, DECISION_PERIOD_MS);
        
        if(n < 0) {
            if(errno == EINTR) continue;
            LOGE("epoll failed: %s", strerror(errno));
            break;
        }

        if(n == 0) {
            if(loop_count % 20 == 0) LOGD("Idle %d", loop_count);
            continue;
        }

        for(int i = 0; i < n; ++i) {
            if(events[i].data.fd == timer_fd_) {
                if(read(timer_fd_, &timer_buf, 8) == 8) {
                    process_decisions();
                }
            }
        }

        if(++feature_counter >= COLLECT_INTERVAL) {
            collect_features();
            feature_counter = 0;
        }
    }

    LOGI("Stopped (iterations: %d)", loop_count);
    save_model();
    cleanup();
}

} // namespace hp