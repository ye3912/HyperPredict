#pragma once
#include <array>
#include "core/types.h"

namespace hp::predict {
class FeatureExtractor {
    uint32_t ewma100_{0}, ewma500_{0}, last_{0};
    int32_t slope50_{0};
public:
    LoadFeature extract(uint32_t util, uint32_t rq, uint32_t wake,
                        uint32_t frame, uint32_t touch, int8_t therm, uint8_t bat) noexcept;
    std::array<float, 10> to_vec(const LoadFeature& f) const noexcept;
};
} // namespace hp::predict