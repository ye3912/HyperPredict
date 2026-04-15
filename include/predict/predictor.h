#pragma once
#include "core/types.h"
#include <array>

namespace hp::predict {

class Predictor {
    std::array<float, 3> weights_{0.0f, 0.0f, 0.0f};
    float ema_error_{0.0f};
    float last_util_{0.0f};
    
public:
    void train(const LoadFeature& features, float actual_fps) noexcept;
    float predict(const LoadFeature& features) noexcept;
    void export_model(const char* path) noexcept;
};

} // namespace hp::predict