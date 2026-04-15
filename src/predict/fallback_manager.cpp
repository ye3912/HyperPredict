#include "predict/fallback_manager.h"
#include "core/logger.h"
#include <chrono>
#include <array>

namespace hp::predict {

// 获取当前时间戳（纳秒）
static uint64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 获取当前时间戳（毫秒）
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
    
    // 每 100ms 检查一次
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
            // 回退模式持续 4 秒
            if (now - fb_ts_ > 4'000'000'000ULL) {
                mode_ = Mode::RECOVERING;
                recovery_start_ = now;                consecutive_failures_ = 0;
                LOGI("Entering recovery mode...");
            }
            break;
            
        case Mode::RECOVERING:
            // 恢复模式持续 2 秒，检查是否稳定
            if (now - recovery_start_ > 2'000'000'000ULL) {
                if (consecutive_failures_ == 0) {
                    mode_ = Mode::NORMAL;
                    use_safe_mode_ = false;
                    LOGI("Recovery successful, back to normal");
                } else {
                    // 恢复失败，重新进入回退
                    mode_ = Mode::FALLBACK;
                    fb_ts_ = now;
                    LOGW("Recovery failed, back to fallback");
                }
            }
            break;
    }
    
    // 记录历史失败率
    if (consecutive_failures_ > 0) {
        // 循环缓冲区记录
        static int idx = 0;
        rec_ts_[idx] = now;
        idx = (idx + 1) % 10;
    }
    
    // 检查是否频繁失败（过去 10 秒内失败超过 5 次）
    int recent_failures = 0;
    for (const auto& ts : rec_ts_) {
        if (ts > 0 && (now - ts) < 10'000) {
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

void FallbackManager::report_success() noexcept {    if (consecutive_failures_ > 0) {
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

void FallbackManager::reset() noexcept {
    consecutive_failures_ = 0;
    use_safe_mode_ = false;
    mode_ = Mode::NORMAL;
    last_check_ = 0;
    recovery_start_ = 0;
    fb_ts_ = 0;
    for (auto& ts : rec_ts_) ts = 0;
    LOGI("FallbackManager reset");
}

} // namespace hp::predict