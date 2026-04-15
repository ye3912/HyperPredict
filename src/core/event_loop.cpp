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
}

void EventLoop::setup_timer() noexcept {
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    struct itimerspec its{};
    its.it_value.tv_nsec = period_ms_ * 1000000;
    its.it_interval.tv_nsec = period_ms_ * 1000000;
    timerfd_settime(timer_fd_, 0, &its, nullptr);
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd_;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, timer_fd_, &ev);
}

void EventLoop::collect() noexcept {
    LoadFeature f = collector_.collect();
    f = extractor_.extract(f.cpu_util, f.run_queue_len, f.wakeups_100ms, 
                           f.frame_interval_us, f.touch_rate_100ms, 
                           f.thermal_margin, f.battery_level);
    q_.try_push(f);
}

void EventLoop::detect_scene(const LoadFeature& f) noexcept {
    static bool was_game = false;
    bool is_game = f.is_gaming && f.cpu_util > 400 && f.frame_interval_us < 33000;
    if (is_game != was_game) {
        LOGI("Scene -> %s", is_game ? "Game" : "Daily");
        was_game = is_game;
    }
}

void EventLoop::init_hw() noexcept {
    if (!hw_.analyze()) LOGW("HW analysis fallback");
    freq_mgr_.init(topology_);
    migrator_.init(hw_.profile());
    binder_.init(hw_.profile());    binder_.bind_sched();
    LOGI("Backend: %s", 
         writer_.backend() == kernel::Backend::UCLAMP ? "UCLAMP" :
         writer_.backend() == kernel::Backend::CGROUPS ? "CGROUPS" : "FREQ");
}

void EventLoop::check_mode() noexcept {
    char p[32] = {0};
    FILE* f = popen("getprop hyperpredict.mode 2>/dev/null", "r");
    if (f) { fgets(p, sizeof(p), f); pclose(f); }
    BindMode m = BindMode::BALANCED;
    std::string s(p);
    if (s.find("perf") != std::string::npos) m = BindMode::PERFORMANCE;
    else if (s.find("save") != std::string::npos) m = BindMode::POWERSAVE;
    else if (s.find("game") != std::string::npos) m = BindMode::GAME;
    binder_.apply(m);
    migrator_.reset();
}

void EventLoop::process() noexcept {
    if (loop_ % 10 == 0) check_mode();
    const auto& doms = topology_.get_domains();
    if (doms.empty()) return;
    int idx = static_cast<int>(doms.size()) - 1;
    const auto& dom = doms[idx];
    const auto& fi = freq_mgr_.info(idx);
    
    if (auto f = q_.try_pop()) {
        bool idle = (f->cpu_util < 80 && f->frame_interval_us > 33333 && 
                     binder_.mode() != BindMode::GAME);
        FreqConfig cfg;
        uint32_t model = 0;
        int32_t fas = 0;
        
        if (idle) {
            cfg.target_freq = fi.min;
            cfg.min_freq = fi.min;
            cfg.uclamp_min = 0;
            cfg.uclamp_max = 10;
        } else {
            float fps = f->frame_interval_us > 0 ? (1000000.f / f->frame_interval_us) : 60.f;
            cfg = engine_.decide(*f, fps, "default");
            model = cfg.target_freq;
            
            int32_t err = static_cast<int32_t>(f->frame_interval_us) - target_frame_us_;
            static int32_t last = 0;
            if (std::abs(err) > FAS_THR) {
                int32_t t = static_cast<int32_t>((static_cast<float>(err) / 1000.f) * 100000.f);
                fas = (std::abs(t - last) > 150000) ? t : last;
            } else {                fas = static_cast<int32_t>(last * 0.85f);
            }
            last = fas;
            
            int32_t adj = static_cast<int32_t>(model) + fas;
            if (f->thermal_margin < 5) adj = static_cast<int32_t>(adj * 0.85f);
            else if (f->thermal_margin < 10) adj = static_cast<int32_t>(adj * 0.92f);
            cfg.target_freq = std::max(0u, static_cast<uint32_t>(adj));
        }
        
        cfg.target_freq = freq_mgr_.snap(cfg.target_freq, idx);
        cfg.target_freq = std::clamp(cfg.target_freq, fi.min, fi.max);
        cfg.min_freq = std::clamp(static_cast<uint32_t>(cfg.target_freq * 0.75f), fi.min, cfg.target_freq);
        
        std::vector<std::pair<int, FreqConfig>> batch;
        for (int c : dom.cpus) batch.emplace_back(c, cfg);
        writer_.apply(batch);
        
        migrator_.update(sched_getcpu(), f->cpu_util, f->run_queue_len);
        if (mig_tick_++ % 5 == 0) {
            auto mr = migrator_.decide(sched_getcpu(), f->thermal_margin, f->is_gaming);
            if (mr.go) {
                cpu_set_t m;
                CPU_ZERO(&m);
                CPU_SET(mr.target, &m);
                sched_setaffinity(0, sizeof(m), &m);
                LOGD("Mig CPU%d->%d", sched_getcpu(), mr.target);
            }
        }
        
        if (loop_ % 20 == 0) {
            LOGI("Freq=%u | Model=%u | FAS=%d | Idle=%d", 
                 cfg.target_freq, model, fas, idle ? 1 : 0);
        }
    }
}

void EventLoop::adjust(bool u) noexcept {
    if (u) period_ms_ = std::max(20, period_ms_ - 10);
    else { idle_cnt_++; if (idle_cnt_ > 100) period_ms_ = std::min(200, period_ms_ + 5); }
    if (timer_fd_ >= 0) {
        struct itimerspec its{};
        its.it_value.tv_nsec = period_ms_ * 1000000;
        its.it_interval.tv_nsec = period_ms_ * 1000000;
        timerfd_settime(timer_fd_, 0, &its, nullptr);
    }
}

void EventLoop::cleanup() noexcept {
    if (timer_fd_ >= 0) close(timer_fd_);    if (epfd_ >= 0) close(epfd_);
}

void EventLoop::start() {
    LOGI("=== HyperPredict v4.0 Production ===");
    if (!topology_.detect()) return;
    init_hw();
    calibrator_.calibrate(topology_);
    engine_.init(calibrator_.baseline());
    setup_epoll();
    setup_timer();
    if (epfd_ < 0 || timer_fd_ < 0) return;
    
    LOGI("Loop started | FAS=%dus", FAS_THR);
    struct epoll_event ev[4];
    uint64_t buf;
    uint32_t fc = 0;
    
    while (run_.load(std::memory_order_relaxed)) {
        loop_++;
        int n = epoll_wait(epfd_, ev, 4, period_ms_);
        if (n < 0) {
            if (errno == EINTR) { usleep(period_ms_ * 1000); continue; }
            break;
        }
        if (n == 0) { idle_cnt_++; if (idle_cnt_ > 50) adjust(false); continue; }
        idle_cnt_ = 0;
        for (int i = 0; i < n; ++i) {
            if (ev[i].data.fd == timer_fd_ && read(timer_fd_, &buf, 8) == 8) {
                process();
            }
        }
        int ci = (period_ms_ > 100) ? 20 : 5;
        if (++fc >= ci) { collect(); fc = 0; }
    }
    LOGI("Shutdown");
    save();
    cleanup();
}

} // namespace hp