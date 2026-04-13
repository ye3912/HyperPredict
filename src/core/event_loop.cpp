#include "core/event_loop.h"
#include <unistd.h>
#include <cstdlib>
#include "core/logger.h"

namespace hp {

void EventLoop::collect() {
    while(run_.load()) {
        LoadFeature f = collector_.collect();
        q_.try_push(f);
        usleep(8000);
    }
}

void EventLoop::dispatch() {
    auto base = calibrator_.calibrate();
    engine_.init(base);
    LOGI("Boot calibration complete. Baseline freq: big=%u mid=%u little=%u",
         base.big.target_freq, base.mid.target_freq, base.little.target_freq);

    while(run_.load()) {
        if(auto f = q_.try_pop()) {
            const char* pkg = "com.target.game";
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
