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

void EventLoop::collect_features() noexcept {
    LoadFeature f = collector_.collect();
    f = extractor_.extract(
        f.cpu_util, f.run_queue_len, f.wakeups_100ms,        f.frame_interval_us, f.touch_rate_100ms,
        f.thermal_margin, f.battery_level
    );
    
    if(q_.try_push(f)) {
        detect_scene(f);
    }
}

void EventLoop::detect_scene(const LoadFeature& f) noexcept {
    Timestamp now = now_ns();
    if(now - last_scene_check_ < SCENE_CHECK_INTERVAL_NS) return;
    last_scene_check_ = now;
    
    bool high_load = f.cpu_util > 400 && f.frame_interval_us < 33000;
    bool was_gaming = is_gaming_mode_;
    is_gaming_mode_ = f.is_gaming && high_load;
    
    if(was_gaming != is_gaming_mode_) {
        LOGI("Scene changed -> %s (util=%u, frame=%uus)", 
             is_gaming_mode_ ? "Gaming" : "Daily", f.cpu_util, f.frame_interval_us);
    }
}

void EventLoop::process_decisions() noexcept {
    const auto& domains = topology_.get_domains();
    if(domains.empty()) return;

    int target_idx = static_cast<int>(domains.size()) - 1;
    const auto& target_domain = domains[target_idx];
    const auto& freq_info = freq_manager_.get_domain_info(target_idx);

    if(auto f = q_.try_pop()) {
        bool is_deep_idle = (f->cpu_util < 100 && f->wakeups_100ms < 10 &&
                             f->frame_interval_us > 33333 && f->touch_rate_100ms == 0 && !is_gaming_mode_);

        FreqConfig cfg;
        uint32_t model_freq = 0;
        int32_t fas_delta = 0;
        float thermal_scale = 1.0f;

        if(is_deep_idle) {
            cfg.target_freq = freq_info.min_freq;
            cfg.min_freq = freq_info.min_freq;
            cfg.uclamp_min = 0; cfg.uclamp_max = 10;
        } else {
            float real_fps = f->frame_interval_us > 0 ? (1000000.f / f->frame_interval_us) : 60.f;
            cfg = engine_.decide(*f, real_fps, "default");
            model_freq = cfg.target_freq;
            int32_t error_us = static_cast<int32_t>(f->frame_interval_us) - target_frame_time_us_;
            if(std::abs(error_us) > FAS_THRESHOLD) {
                fas_delta = static_cast<int32_t>((static_cast<float>(error_us) / 1000.f) * 100000.f);
                fas_delta = std::clamp(fas_delta, -200000, 200000);
            }

            int32_t adjusted = static_cast<int32_t>(model_freq) + fas_delta;

            int margin = f->thermal_margin;
            if(margin < 5) thermal_scale = 0.85f;
            else if(margin < 10) thermal_scale = 0.92f;
            else if(margin < 15) thermal_scale = 0.97f;
            adjusted = static_cast<int32_t>(adjusted * thermal_scale);

            cfg.target_freq = static_cast<uint32_t>(std::max(0, adjusted));
        }

        cfg.target_freq = freq_manager_.snap_to_step(cfg.target_freq, target_idx);
        cfg.target_freq = std::clamp(cfg.target_freq, freq_info.min_freq, freq_info.max_freq);
        cfg.min_freq = std::clamp(static_cast<uint32_t>(cfg.target_freq * 0.8f), freq_info.min_freq, cfg.target_freq);

        std::vector<std::pair<int, FreqConfig>> batch;
        batch.reserve(target_domain.cpus.size());
        for(int cpu : target_domain.cpus) batch.emplace_back(cpu, cfg);
        writer_.set_batch(batch);

        if(loop_count_ % 20 == 0) {
            LOGI("Freq=%u kHz | Model=%u | FAS=%d | Therm=%.2f | Scene=%s",
                 cfg.target_freq, model_freq, fas_delta, thermal_scale, is_gaming_mode_?"Game":"Daily");
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

void EventLoop::export_model_json() noexcept {
    if(engine_.export_model_json(MODEL_JSON_PATH)) {
        LOGI("JSON exported: %s", MODEL_JSON_PATH);
    } else {
        LOGE("Failed to export JSON");
    }
}

void EventLoop::start() {
    LOGI("=== HyperPredict v4.2 (Absolute FAS + Hardware Stepping) ===");
    
    if(!topology_.detect()) {
        LOGE("Topology detection failed");
        return;
    }
    freq_manager_.init(topology_);

    LOGI("Detected %d domains, %d CPUs", topology_.get_domains().size(), topology_.get_total_cpus());    for(size_t i = 0; i < topology_.get_domains().size(); ++i) {
        const auto& d = topology_.get_domains()[i];
        LOGI("Domain %zu: CPUs=%zu, Freq=%u-%u kHz", i, d.cpus.size(), d.min_freq, d.max_freq);
    }

    int target_cpu = -1;
    const auto& domains = topology_.get_domains();
    if(!domains.empty()) {
        const auto& prime = domains.back();
        if(prime.cpus.size() >= 2) target_cpu = prime.cpus[1];
        else if(!prime.cpus.empty()) target_cpu = prime.cpus[0];
        else target_cpu = domains.front().cpus.back();
    }

    if(target_cpu >= 0) {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(target_cpu, &mask);
        if(sched_setaffinity(0, sizeof(mask), &mask) == 0) {
            LOGI("Scheduler bound to CPU%d", target_cpu);
        }
    }

    calibrator_.calibrate(topology_);
    engine_.init(calibrator_.baseline());
    LOGI("Baseline: %u kHz", calibrator_.baseline().big.target_freq);

    setup_epoll();
    setup_timer();
    if(epfd_ < 0 || timer_fd_ < 0) return;

    LOGI("Loop started (adaptive %d-%dms)", MIN_PERIOD_MS, MAX_PERIOD_MS);
    
    struct epoll_event events[4];
    uint64_t timer_buf;
    uint32_t feature_counter = 0;

    while(run_.load(std::memory_order_relaxed)) {
        loop_count_++;
        
        int n = epoll_wait(epfd_, events, 4, current_period_ms_);
        
        if(n < 0) {
            if(errno == EINTR) {
                usleep(current_period_ms_ * 1000);
                continue;
            }
            LOGE("epoll failed: %s", strerror(errno));
            break;
        }
        if(n == 0) {
            idle_counter_++;
            if(idle_counter_ > 50) adjust_period(false);
            if(loop_count_ % 20 == 0) LOGD("Idle %d period=%dms", loop_count_, current_period_ms_);
            continue;
        }

        idle_counter_ = 0;
        for(int i = 0; i < n; ++i) {
            if(events[i].data.fd == timer_fd_) {
                if(read(timer_fd_, &timer_buf, 8) == 8) {
                    process_decisions();
                }
            }
        }
        
        int collect_interval = (current_period_ms_ > 100) ? 20 : 5;
        if(++feature_counter >= collect_interval) {
            collect_features();
            feature_counter = 0;
        }
    }

    LOGI("Stopped (iterations: %d)", loop_count_);
    save_model();
    cleanup();
}

} // namespace hp