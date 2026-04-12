#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include "core/types.h"

namespace hp::predict {
class FTRL {
    static constexpr size_t DIM = 10;
    static constexpr float ALPHA = 0.11f;
    static constexpr float BETA  = 0.75f;
    static constexpr float L1    = 0.0006f;
    static constexpr float L2    = 0.09f;

    alignas(64) std::array<float, DIM> w_{}, z_{}, n_{};
    void upd(size_t i, float g, float s) noexcept;

public:
    float predict(const std::array<float, DIM>& x) const noexcept;
    void update(const std::array<float, DIM>& x, bool label) noexcept;
};
} // namespace hp::predict