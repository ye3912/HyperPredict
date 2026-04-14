#include "core/event_loop.h"
#include "core/logger.h"
#include <cstring>
#include <cstdlib>
#include <sched.h>
#include <cerrno>
#include <unistd.h>

namespace hp {

void EventLoop::setup_epoll() noexcept {
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if(epfd_ < 0) LOGE("epoll_create1 failed: %s", strerror(errno));
}

void EventLoop::setup_timer() noexcept {
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if(timer_fd_ < 0) { LOGE("timerfd_create failed"); return; }
    
    struct itimerspec its{};
    its.it_value.tv_nsec = DECISION_PERIOD_MS * 1000000;
    its.it_interval.tv_nsec = DECISION_PERIOD_MS * 1000000;
    if(timerfd_settime(timer_fd_, 0, &its, nullptr) < 0) {
        LOGE("timerfd_settime failed");
        return;
    }
    
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd_;
    if(epoll_ctl(epfd_, EPOLL_CTL_ADD, timer_fd_, &ev) < 0) {
        LOGE("epoll_ctl add timer failed");
    }
}

void EventLoop::collect_features() noexcept {
    LoadFeature f = collector_.collect();
    f = extractor_.extract(
        f.cpu_util,
        f.run_queue_len,
        f.wakeups_100ms,
        f.frame_interval_us,
        f.touch_rate_100ms,
        f.thermal_margin,
        f.battery_level
    );
    q_.try_push(f);
}

void EventLoop::process_decisions() noexcept {    const auto& big_cores = topology_.get_big_cores();
    if(big_cores.empty()) return;

    std::vector<std::pair<int, FreqConfig>> batch;
    batch.reserve(big_cores.size());

    constexpr size_t BATCH_SIZE = 16;
    LoadFeature features[BATCH_SIZE];
    size_t count = 0;
    
    while(count < BATCH_SIZE) {
        auto feat = q_.try_pop();
        if(!feat) break;
        features[count++] = *feat;
    }

    if(count == 0) return;

    const char* pkg = "com.target.game";
    for(size_t i = 0; i < count; ++i) {
        float sim_fps = 54.f + (features[i].boost_prob / 100.f) * 6.f;
        FreqConfig cfg = engine_.decide(features[i], sim_fps, pkg);
        
        for(int cpu : big_cores) {
            batch.emplace_back(cpu, cfg);
        }
    }
    
    writer_.set_batch(batch);
}

void EventLoop::cleanup() noexcept {
    if(timer_fd_ >= 0) close(timer_fd_);
    if(epfd_ >= 0) close(epfd_);
    timer_fd_ = -1;
    epfd_ = -1;
}

void EventLoop::start() {
    LOGI("Starting epoll-based event loop with real data collection");
    
    if(!topology_.detect()) {
        LOGE("Failed to detect CPU topology");
        return;
    }

    int prime = topology_.get_highest_freq_core();
    if(prime >= 0) {
        cpu_set_t mask;
        CPU_ZERO(&mask);        CPU_SET(prime, &mask);
        if(sched_setaffinity(0, sizeof(mask), &mask) == 0) {
            LOGI("Bound to CPU%d (Prime Core)", prime);
        }
    }

    calibrator_.calibrate(topology_);
    engine_.init(calibrator_.baseline());
    LOGI("Baseline ready. Big target: %u kHz", calibrator_.baseline().big.target_freq);

    setup_epoll();
    setup_timer();
    if(epfd_ < 0 || timer_fd_ < 0) return;

    struct epoll_event events[MAX_EVENTS];
    uint64_t timer_buf;
    uint32_t feature_counter = 0;

    while(run_.load(std::memory_order_relaxed)) {
        int n = epoll_wait(epfd_, events, MAX_EVENTS, 50);
        if(n < 0) {
            if(errno == EINTR) continue;
            LOGE("epoll_wait failed: %s", strerror(errno));
            break;
        }

        for(int i = 0; i < n; ++i) {
            if(events[i].data.fd == timer_fd_) {
                if(read(timer_fd_, &timer_buf, 8) == 8) {
                    process_decisions();
                }
            }
        }

        if(++feature_counter >= 8) {
            collect_features();
            feature_counter = 0;
        }
    }

    LOGI("Event loop stopped.");
    cleanup();
}

} // namespace hp