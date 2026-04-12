#pragma once
#include <array>
#include "core/types.h"

namespace hp::predict {
enum class FMode : uint8_t { ACTIVE, FALLBACK, RECOVERING };

class FallbackManager {
    static constexpr size_t WIN = 12;
    FMode mode_{FMode::ACTIVE};
    std::array<float, WIN> err_{};
    uint8_t idx_{0}, consec_{0};
    FreqConfig last_opt_;
    Timestamp fb_ts_{0}, rec_ts_{0};

public:
    void record(float pred, float actual) noexcept;
    bool should_fallback() const noexcept { return mode_ == FMode::FALLBACK; }
    FMode mode() const noexcept { return mode_; }
    void set_optimal(const FreqConfig& c) noexcept { last_opt_ = c; }
    FreqConfig optimal() const noexcept { return last_opt_; }
    void try_recover() noexcept;
};
} // namespace hp::predict