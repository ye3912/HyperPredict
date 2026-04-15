#include "predict/fallback_manager.h"
#include <chrono>  // ✅ 添加 chrono 头文件
#include <array>
#include <cstdint>

namespace hp::predict {

// ✅ 添加 now_ns() 时间工具函数
inline uint64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void FallbackManager::record(float pred, float actual) noexcept {
    if(mode_ == FMode::FALLBACK) return;
    
    float e = std::abs(pred - actual) / 1024.f;
    err_[idx_] = e;
    idx_ = (idx_ + 1) & (WIN - 1);
    
    float avg = 0.f, cnt = 0.f;
    for(float v : err_) if(v > 0.f) { avg += v; cnt++; }
    avg = (cnt > 0.f) ? (avg / cnt) : 0.f;
    
    if(avg > 0.14f) consec_++; else consec_ = 0;
    if(consec_ >= 3) { mode_ = FMode::FALLBACK; fb_ts_ = now_ns(); }
}

void FallbackManager::try_recover() noexcept {
    if(mode_ != FMode::FALLBACK) return;
    
    if(now_ns() - fb_ts_ > 4'000'000'000ULL) mode_ = FMode::RECOVERING;
    
    if(mode_ == FMode::RECOVERING) {
        if(now_ns() - rec_ts_ > 2'000'000'000ULL) {
            mode_ = FMode::ACTIVE;
            consec_ = 0; idx_ = 0; err_.fill(0.f);
            rec_ts_ = now_ns();
        } else {
            rec_ts_ = now_ns();
        }
    }
}

} // namespace hp::predict