#include "predict/fallback_manager.h"
#include "core/logger.h"
#include <chrono>

namespace hp::predict {

static uint64_t now_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

FallbackManager::FallbackManager() noexcept
    : consecutive_failures_{0}
    , use_safe_mode_{false}
    , last_check_{0}
    , recovery_start_{0}
    , mode_{Mode::NORMAL}
    , hist_{}
    , rec_ts_{} {
}

void FallbackManager::check_and_apply() noexcept {
    const uint64_t now = now_ms();
    
    if (now - last_check_ < 100) return;
    last_check_ = now;
    
    switch (mode_) {
        case Mode::NORMAL:
            if (consecutive_failures_ >= 3) {
                mode_ = Mode::FALLBACK;
                fb_ts_ = now;
                LOGW("Fallback triggered: failures=%u", consecutive_failures_);
            }
            break;
            
        case Mode::FALLBACK:
            if (now - fb_ts_ > 4000) {
                mode_ = Mode::RECOVERING;
                recovery_start_ = now;
                consecutive_failures_ = 0;
                LOGI("Entering recovery mode...");
            }
            break;
            
        case Mode::RECOVERING:
            if (now - recovery_start_ > 2000) {
                if (consecutive_failures_ == 0) {
                    mode_ = Mode::NORMAL;
                    use_safe_mode_ = false;                    LOGI("Recovery successful, back to normal");
                } else {
                    mode_ = Mode::FALLBACK;
                    fb_ts_ = now;
                    LOGW("Recovery failed, back to fallback");
                }
            }
            break;
    }
    
    if (consecutive_failures_ > 0) {
        static int idx = 0;
        rec_ts_[idx] = now;
        idx = (idx + 1) % 10;
    }
    
    int recent_failures = 0;
    for (const auto& ts : rec_ts_) {
        if (ts > 0 && (now - ts) < 10000) {
            recent_failures++;
        }
    }
    
    if (recent_failures >= 5) {
        use_safe_mode_ = true;
        LOGW("Frequent failures detected, using safe mode");
    }
}

void FallbackManager::report_failure() noexcept {
    consecutive_failures_++;
    LOGE("Scheduler failure #%u", consecutive_failures_);
}

void FallbackManager::report_success() noexcept {
    if (consecutive_failures_ > 0) {
        consecutive_failures_ = 0;
        LOGI("Scheduler recovered");
    }
}

bool FallbackManager::is_safe_mode() const noexcept {
    return use_safe_mode_;
}

FallbackManager::Mode FallbackManager::current_mode() const noexcept {
    return mode_;
}

void FallbackManager::reset() noexcept {    consecutive_failures_ = 0;
    use_safe_mode_ = false;
    mode_ = Mode::NORMAL;
    last_check_ = 0;
    recovery_start_ = 0;
    fb_ts_ = 0;
    for (auto& ts : rec_ts_) ts = 0;
    LOGI("FallbackManager reset");
}

} // namespace hp::predict