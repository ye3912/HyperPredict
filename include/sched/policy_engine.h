#pragma once
#include "predict/predictor.h"
#include "predict/fallback_manager.h"
#include "cache/lru_cache.h"
#include "core/types.h"

namespace hp::sched {
class PolicyEngine {
    predict::FTRL pred_;
    predict::FallbackManager fb_;
    cache::LRUCache<> cache_;
    BaselinePolicy base_;
    struct Hist { Timestamp last{0}; FreqConfig cfg; uint8_t same{0}; } hist_;

    std::array<float, 10> to_vec(const LoadFeature& f) const noexcept;

public:
    void init(const BaselinePolicy& b) noexcept { base_ = b; }
    FreqConfig decide(const LoadFeature& f, float actual_fps, const char* pkg) noexcept;
    predict::FMode fallback_state() const noexcept { return fb_.mode(); }
};
} // namespace hp::sched