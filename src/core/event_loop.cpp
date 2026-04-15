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
    if(epfd_ < 0) { LOGE("epoll_create1 failed: %s", strerror(errno)); return; }
}

void EventLoop::setup_timer() noexcept {
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if(timer_fd_ < 0) { LOGE("timerfd_create failed: %s", strerror(errno)); return; }
    
    struct itimerspec its{};
    its.it_value.tv_nsec = current_period_ms_ * 1000000;
    its.it_interval.tv_nsec = current_period_ms_ * 1000000;
    
    if(timerfd_settime(timer_fd_, 0, &its, nullptr) < 0) {
        LOGE("timerfd_settime failed: %s", strerror(errno)); return;
    }
    
    struct epoll_event ev{};
    ev.events = EPOLLIN; ev.data.fd = timer_fd_;
    if(epoll_ctl(epfd_, EPOLL_CTL_ADD, timer_fd_, &ev) < 0) {
        LOGE("epoll_ctl add timer failed: %s", strerror(errno)); return;
    }
}

void EventLoop::collect_features() noexcept {
    LoadFeature f = collector_.collect();
    f = extractor_.extract(f.cpu_util, f.run_queue_len, f.wakeups_100ms,
                           f.frame_interval_us, f.touch_rate_100ms,
                           f.thermal_margin, f.battery_level);
    if(q_.try_push(f)) detect_scene(f);
}

void EventLoop::detect_scene(const LoadFeature& f) noexcept {
    Timestamp now = now_ns();
    if(now - last_scene_check_ < SCENE_CHECK_INTERVAL_NS) return;
    last_scene_check_ = now;
    
    bool high_load = f.cpu_util > 400 && f.frame_interval_us < 33000;    bool was_gaming = is_gaming_mode_;
    is_gaming_mode_ = f.is_gaming && high_load;
    
    if(was_gaming != is_gaming_mode_) {
        LOGI("Scene -> %s (util=%u, frame=%uus)", 
             is_gaming_mode_ ? "Gaming" : "Daily", f.cpu_util, f.frame_interval_us);
    }
}

void EventLoop::process_decisions() noexcept {
    const auto& big_cores = topology_.get_big_cores();
    if(big_cores.empty()) return;

    if(auto f = q_.try_pop()) {
        // 1. 模型基准频率
        float real_fps = f->frame_interval_us > 0 ? (1000000.f / f->frame_interval_us) : 60.f;
        FreqConfig cfg = engine_.decide(*f, real_fps, "default");
        uint32_t model_freq = cfg.target_freq;
        
        // 2. FAS 比例修正 (无硬编码)
        int32_t error_us = static_cast<int32_t>(f->frame_interval_us) - target_frame_time_us_;
        float correction = 0.f;
        
        if(std::abs(error_us) > FAS_THRESHOLD) {
            float error_ratio = static_cast<float>(error_us) / target_frame_time_us_;
            float gain = is_gaming_mode_ ? FAS_GAME_GAIN : FAS_DAILY_GAIN;
            correction = error_ratio * gain;
            correction = std::clamp(correction, -0.15f, 0.15f);
            cfg.target_freq = static_cast<uint32_t>(model_freq * (1.0f + correction));
        }
        
        // 3. 温控乘性缩放
        float thermal_scale = 1.0f;
        int margin = f->thermal_margin;
        if(margin < 5) thermal_scale = 0.85f;
        else if(margin < 10) thermal_scale = 0.92f;
        else if(margin < 15) thermal_scale = 0.97f;
        
        cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * thermal_scale);
        cfg.min_freq = static_cast<uint32_t>(cfg.target_freq * 0.8f);
        
        // 4. 仅硬件安全限制
        cfg.target_freq = std::clamp(cfg.target_freq, 800000u, 3000000u);
        
        // 5. 下发
        std::vector<std::pair<int, FreqConfig>> batch;
        batch.reserve(big_cores.size());
        for(int cpu : big_cores) batch.emplace_back(cpu, cfg);
        
        if(writer_.set_batch(batch) && loop_count_ % 20 == 0) {            LOGI("Model=%u kHz | FAS=%.1f%% | Err=%dus | Therm=%.2f | Final=%u kHz | Scene=%s",
                 model_freq, correction*100, error_us, thermal_scale, 
                 cfg.target_freq, is_gaming_mode_?"Game":"Daily");
        }
    }
}

void EventLoop::adjust_period(bool urgent) noexcept {
    if(urgent) current_period_ms_ = std::max(MIN_PERIOD_MS, current_period_ms_ - 10);
    else {
        idle_counter_++;
        if(idle_counter_ > 100) current_period_ms_ = std::min(MAX_PERIOD_MS, current_period_ms_ + 5);
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
    if(engine_.export_model(MODEL_BIN_PATH)) LOGI("Model saved");
    if(engine_.export_model_json(MODEL_JSON_PATH)) LOGI("JSON exported");
}

void EventLoop::export_model_json() noexcept {
    if(engine_.export_model_json(MODEL_JSON_PATH)) LOGI("JSON exported");
    else LOGE("Failed to export JSON");
}

void EventLoop::start() {
    LOGI("=== HyperPredict v3.2 (Model-First + Proportional FAS) ===");
    if(writer_.uclamp_supported()) LOGI("Control: uclamp");
    else if(writer_.cgroups_supported()) LOGI("Control: cpu.shares");
    else LOGW("Control: frequency only");
    
    if(engine_.load_model(MODEL_BIN_PATH)) LOGI("Model loaded");
    else LOGI("Fresh start");
    
    if(!topology_.detect()) { LOGE("Topology detection failed"); return; }

    int target_cpu = -1;
    const auto& mid = topology_.get_mid_cores();
    const auto& big = topology_.get_big_cores();    if(!mid.empty()) target_cpu = mid[0];
    else if(big.size() >= 2) target_cpu = big[1];
    else if(!big.empty()) target_cpu = big[0];
    
    if(target_cpu >= 0) {
        cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(target_cpu, &mask);
        if(sched_setaffinity(0, sizeof(mask), &mask) == 0) LOGI("Bound to CPU%d", target_cpu);
    }

    calibrator_.calibrate(topology_);
    engine_.init(calibrator_.baseline());
    LOGI("Baseline: %u kHz", calibrator_.baseline().big.target_freq);

    setup_epoll(); setup_timer();
    if(epfd_ < 0 || timer_fd_ < 0) return;

    LOGI("Loop started (adaptive %d-%dms)", MIN_PERIOD_MS, MAX_PERIOD_MS);
    
    struct epoll_event events[4];
    uint64_t timer_buf;
    uint32_t feature_counter = 0;

    while(run_.load(std::memory_order_relaxed)) {
        loop_count_++;
        int n = epoll_wait(epfd_, events, 4, current_period_ms_);
        
        if(n < 0) {
            if(errno == EINTR) { usleep(current_period_ms_ * 1000); continue; }
            LOGE("epoll failed: %s", strerror(errno)); break;
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
                if(read(timer_fd_, &timer_buf, 8) == 8) process_decisions();
            }
        }
        
        int collect_interval = (current_period_ms_ > 100) ? 20 : 5;
        if(++feature_counter >= collect_interval) { collect_features(); feature_counter = 0; }
    }

    LOGI("Stopped (iterations: %d)", loop_count_);
    save_model(); cleanup();}

} // namespace hp