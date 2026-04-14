#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "core/types.h"

namespace hp::predict {

class FTRL {
    static constexpr size_t DIM = 10;
    // 优化：更快学习率，更激进预测
    static constexpr float ALPHA = 0.15f;   // 0.11→0.15
    static constexpr float BETA  = 0.5f;    // 0.75→0.5
    static constexpr float L1    = 0.0004f; // 降低正则化
    static constexpr float L2    = 0.06f;   // 0.09→0.06

    alignas(64) std::array<float, DIM> w_{}, z_{}, n_{};
    void upd(size_t i, float g, float s) noexcept;

public:
    float predict(const std::array<float, DIM>& x) const noexcept;
    void update(const std::array<float, DIM>& x, bool label) noexcept;

    bool save_bin(const char* path) const noexcept;
    bool load_bin(const char* path) noexcept;
    bool export_json(const char* path) const noexcept;
    void reset() noexcept { w_.fill(0.f); z_.fill(0.f); n_.fill(0.f); }
};

} // namespace hp::predict