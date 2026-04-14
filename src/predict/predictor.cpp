#include "predict/predictor.h"

namespace hp::predict {

float FTRL::predict(const std::array<float, DIM>& x) const noexcept {
    float wx = 0.f;
    for(size_t i = 0; i < DIM; ++i) wx += w_[i] * x[i];
    return 1.f / (1.f + std::exp(-std::clamp(wx, -5.f, 5.f)));
}

void FTRL::upd(size_t i, float g, float s) noexcept {
    float ow = w_[i];
    float at = ALPHA / (BETA + std::sqrt(n_[i]));
    z_[i] += g - s * ow;
    if(std::abs(z_[i]) <= L1) {
        w_[i] = 0.f;
    } else {
        float sg = (z_[i] > 0.f) ? -1.f : 1.f;
        w_[i] = -(z_[i] - sg * L1) / (L2 + (BETA + std::sqrt(n_[i])) / at);
    }
    n_[i] += g * g;
}

void FTRL::update(const std::array<float, DIM>& x, bool label) noexcept {
    float p = predict(x);
    float g = p - static_cast<float>(label);
    float s = (1.f / ALPHA) * (std::sqrt(1.f + n_[0]) - 1.f);
    for(size_t i = 0; i < DIM; ++i) {
        if(std::abs(x[i]) > 1e-5f) upd(i, g * x[i], s);
    }
}

} // namespace hp::predict