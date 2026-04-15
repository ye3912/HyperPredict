#pragma once
#include "core/types.h"
#include <array>

namespace hp::sched {

struct PredictorState {
    uint64_t last_update{0};
    float ewma_util{0.0f};
    float ewma_fps{0.0f};
    float trend{0.0f};
    float util_slope_50ms{0.0f};
    float boost_prob{0.0f};
    float predicted_util_50ms{0.0f};
};

struct ConfigHistory {
    uint64_t last{0};
    FreqConfig cfg{};
    uint32_t cfg_hash{0};
};

class PolicyEngine {
    BaselinePolicy baseline_{};
    PredictorState pred_state_{};
    std::array<ConfigHistory, 3> hist_{};
    uint32_t loop_count_{0};
    
public:
    void init(const BaselinePolicy& baseline) noexcept;
    FreqConfig decide(const LoadFeature& f, float target_fps, const char* scene) noexcept;
    void export_model(const char* path) noexcept;
};

} // namespace hp::sched