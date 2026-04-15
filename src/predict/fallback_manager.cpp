#include "predict/fallback_manager.h"
#include "core/logger.h"
#include <chrono>

namespace hp::predict {

static uint64_t now_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void FallbackManager::check_and_apply() noexcept {
    // 简化版：不做复杂回退逻辑
    if (consecutive_failures_ > 5) {
        LOGW("Too many failures, using safe defaults");
        use_safe_mode_ = true;
    }
}

bool FallbackManager::is_safe_mode() const noexcept {
    return use_safe_mode_;
}

void FallbackManager::reset() noexcept {
    consecutive_failures_ = 0;
    use_safe_mode_ = false;
}

} // namespace hp::predict