#pragma once
#include <cstdint>
#include <array>

namespace hp::predict {

class FallbackManager {
public:
    enum class Mode {
        NORMAL,
        FALLBACK,
        RECOVERING
    };
    
    FallbackManager() noexcept;
    
    void check_and_apply() noexcept;
    void report_failure() noexcept;
    void report_success() noexcept;
    bool is_safe_mode() const noexcept;
    Mode current_mode() const noexcept;
    void reset() noexcept;
    
private:
    uint32_t consecutive_failures_;
    bool use_safe_mode_;
    uint64_t last_check_;
    uint64_t recovery_start_;
    uint64_t fb_ts_;  // fallback timestamp
    Mode mode_;
    std::array<uint64_t, 10> hist_;  // 历史检查时间
    std::array<uint64_t, 10> rec_ts_; // 恢复时间戳
};

} // namespace hp::predict