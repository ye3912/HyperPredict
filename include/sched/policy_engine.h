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
    
    // 模型持久化接口
    bool load_model(const char* path) noexcept { return pred_.load_bin(path); }
    bool export_model(const char* path) const noexcept { return pred_.save_bin(path); }
    bool export_model_json(const char* path) const noexcept { return pred_.export_json(path); }
};

} // namespace hp::sched