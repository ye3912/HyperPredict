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
        LOGD("Feature collected: util=%u", f.cpu_util);
    }
}

void EventLoop::process_decisions() noexcept {
    const auto& big_cores = topology_.get_big_cores();
    if(big_cores.empty()) {
        LOGW("No big cores found");
        return;
    }

    if(auto f = q_.try_pop()) {
        const char* pkg = "com.target.game";
        float sim_fps = 54.f + (f->boost_prob / 100.f) * 6.f;
        FreqConfig cfg = engine_.decide(*f, sim_fps, pkg);
        
        std::vector<std::pair<int, FreqConfig>> batch;
        for(int cpu : big_cores) {
            batch.emplace_back(cpu, cfg);
        }
        
        if(writer_.set_batch(batch)) {
            LOGD("Freq set: target=%u kHz | util=%u | prob=%u", 
                 cfg.target_freq, f->cpu_util, f->boost_prob);
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
        epfd_ = -1;    }
    LOGD("Cleanup complete");
}

void EventLoop::save_model() noexcept {
    // 保存二进制模型
    if(engine_.export_model(MODEL_BIN_PATH)) {
        LOGI("Binary model saved: %s", MODEL_BIN_PATH);
    }
    
    // 导出 JSON
    if(engine_.export_model_json(MODEL_JSON_PATH)) {
        LOGI("JSON model exported: %s", MODEL_JSON_PATH);
    }
}

void EventLoop::start() {
    LOGI("=== Event Loop Start ===");
    
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

    // 绑定小核（性能优化）
    int target_cpu = 0;  // 绑定到小核降低功耗
    if(!topology_.get_little_cores().empty()) {
        target_cpu = topology_.get_little_cores()[0];
    }
    
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(target_cpu, &mask);
    if(sched_setaffinity(0, sizeof(mask), &mask) == 0) {
        LOGI("Bound to CPU%d (low power core)", target_cpu);
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
        return;
    }
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
        
        // 增加超时时间降低唤醒频率
        int n = epoll_wait(epfd_, events, MAX_EVENTS, 200);
        
        if(n < 0) {
            if(errno == EINTR) {
                LOGD("epoll interrupted");
                continue;
            }
            LOGE("epoll_wait failed: %s", strerror(errno));
            break;
        }

        if(n == 0) {
            // 定期输出心跳日志
            if(loop_count % 10 == 0) {
                LOGD("Idle (iteration %d)", loop_count);
            }
            continue;
        }

        for(int i = 0; i < n; ++i) {            if(events[i].data.fd == timer_fd_) {
                ssize_t r = read(timer_fd_, &timer_buf, 8);
                if(r == 8) {
                    LOGD("Timer fired");
                    process_decisions();
                }
            }
        }

        // 降低采集频率
        if(++feature_counter >= COLLECT_INTERVAL) {
            LOGD("Collecting features...");
            collect_features();
            feature_counter = 0;
        }
    }

    LOGI("Event loop stopped (iterations: %d)", loop_count);
    
    // 自动保存模型
    save_model();
    cleanup();
}

} // namespace hp