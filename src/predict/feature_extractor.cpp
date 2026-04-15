// src/predict/feature_extractor.cpp - 完整修复版
#include "predict/feature_extractor.h"
#include <algorithm>

namespace hp::predict {

LoadFeature FeatureExtractor::extract(uint32_t util, uint32_t rq, uint32_t wake,
                                       uint32_t frame, uint32_t touch, int8_t therm, uint8_t bat) noexcept {
    LoadFeature f{};
    
    // ✅ 只设置 LoadFeature 中实际存在的字段
    f.cpu_util = std::min(1024u, util);
    f.run_queue_len = std::min(0xFFFFu, rq);
    f.wakeups_100ms = std::min(0xFFFFu, wake);
    f.frame_interval_us = frame;
    f.touch_rate_100ms = std::min(0xFFFFu, touch);
    f.thermal_margin = therm;
    f.battery_level = bat;
    // f.is_gaming 由上层设置，此处保持默认 false

    // ✅ 内部状态更新（存储在类成员变量中，不写入 LoadFeature）
    ewma100_ = (ewma100_ * 3 + util) >> 2;
    ewma500_ = (ewma500_ * 7 + util) >> 3;
    slope50_ = std::clamp(static_cast<int32_t>(util) - static_cast<int32_t>(last_), -120, 120);
    
    // ✅ 内部计算值（用于 to_vec，但不写入 LoadFeature）
    uint32_t predicted_util = std::min(1024u, ewma100_ + std::max(0, slope50_ >> 1));
    uint32_t boost_prob = std::min(100u, (predicted_util * 100u) / 1024u + (slope50_ > 15 ? 15u : 0u));
    
    last_ = util;
    
    // 抑制未使用变量警告
    (void)predicted_util;
    (void)boost_prob;
    
    return f;
}

std::array<float, 10> FeatureExtractor::to_vec(const LoadFeature& f) const noexcept {
    // ✅ 只使用 LoadFeature 中存在的字段 + 类内部状态
    return {
        f.cpu_util / 1024.f,
        f.run_queue_len / 16.f,
        f.wakeups_100ms / 100.f,
        (f.frame_interval_us > 0) ? (16666.f / f.frame_interval_us) : 0.f,
        f.touch_rate_100ms / 20.f,
        std::max(0.f, f.thermal_margin / 10.f),
        ewma100_ / 1024.f,           // ✅ 使用类成员变量
        slope50_ / 100.f,            // ✅ 使用类成员变量
        0.0f,                        // 预留
        0.0f                         // 预留
    };
}

} // namespace hp::predict