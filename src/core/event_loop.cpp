#include "core/event_loop.h"
#include <unistd.h>
#include <cstdlib>
#include "core/logger.h"

namespace hp {
void EventLoop::collect() {
    while(run_.load()) {
        LoadFeature f{};
        // ⚠️ 占位：实际应从 tracefs/procfs 读取，此处用随机值模拟数据流
        f.cpu_util = rand() % 900;
        f.thermal_margin = 5 + (rand() % 3);
        f.battery_level = 85;
        f.frame_interval_us = 16000 + (rand() % 2000);
        f.predicted_util_50ms = f.cpu_util + rand() % 80;
        f.boost_prob = std::min(100u, (uint8_t)(f.predicted_util_50ms / 10));
        q_.try_push(f);
        usleep(8000); // 125Hz 采样
    }
}

void EventLoop::dispatch() {
    auto base = calibrator_.calibrate();
    engine_.init(base);
    LOGI("Boot calibration complete. Baseline freq: big=%u mid=%u little=%u",
         base.big.target_freq, base.mid.target_freq, base.little.target_freq);

    while(run_.load()) {
        if(auto f = q_.try_pop()) {
            const char* pkg = "com.target.game"; // TODO: 从 /proc/<pid>/cmdline 动态解析
            float sim_fps = 54.f + (f->boost_prob / 100.f) * 6.f;
            FreqConfig cfg = engine_.decide(*f, sim_fps, pkg);
            
            std::vector<std::pair<int, FreqConfig>> batch;
            for(int i = 4; i < 8; ++i) batch.emplace_back(i, cfg);
            writer_.set_batch(batch);
        } else {
            usleep(1500);
        }
    }
}

void EventLoop::start() {
    std::thread t1(&EventLoop::collect, this);
    std::thread t2(&EventLoop::dispatch, this);
    t1.join(); t2.join();
}
} // namespace hp