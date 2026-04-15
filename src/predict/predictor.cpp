#include "predict/predictor.h"
#include "core/logger.h"
#include <cmath>
#include <algorithm>

namespace hp::predict {

void Predictor::train(const LoadFeature& features, float actual_fps) noexcept {
    // 计算当前利用率
    float util = static_cast<float>(features.cpu_util) / 1024.f;
    float fps = features.frame_interval_us > 0 ? 
                (1000000.f / features.frame_interval_us) : actual_fps;
    
    // 计算误差
    float error = actual_fps - fps;
    float lr = 0.01f; // 学习率
    
    // 更新权重（线性回归）
    weights_[0] += lr * error * util; // 利用率权重
    weights_[1] += lr * error * features.run_queue_len; // 队列长度权重
    weights_[2] += lr * error; // 偏置项
    
    // 限制权重范围
    for (auto& w : weights_) {
        w = std::clamp(w, -1.0f, 1.0f);
    }
    
    // 更新 EMA
    ema_error_ = ema_error_ * 0.9f + error * 0.1f;
}

float Predictor::predict(const LoadFeature& features) noexcept {
    float util = static_cast<float>(features.cpu_util) / 1024.f;
    
    // 线性预测
    float pred = weights_[0] * util + 
                 weights_[1] * features.run_queue_len +
                 weights_[2];
    
    // 添加趋势修正
    float trend = (util - last_util_) * 10.0f;
    pred += trend * 0.5f;
    last_util_ = util;
    
    return std::clamp(pred, 0.0f, 120.0f);
}

void Predictor::export_model(const char* path) noexcept {
    (void)path;
    LOGI("Predictor weights: w_util=%.3f, w_rq=%.3f, bias=%.3f, ema_err=%.3f", 
         weights_[0], weights_[1], weights_[2], ema_error_);
}

} // namespace hp::predict