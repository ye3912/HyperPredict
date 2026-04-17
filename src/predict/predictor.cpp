#include "predict/predictor.h"
#include "core/logger.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace hp::predict {

// ============================================================
// NeuralPredictor 实现 (8→4→1 MLP)
// ============================================================

NeuralPredictor::NeuralPredictor() noexcept {
    // 初始化权重: input→hidden (8×4), hidden→output (4×1)
    // 扁平化存储: [wh(32), wo(4)]
    weights_.resize(HIDDEN_SIZE * INPUT_SIZE + OUTPUT_SIZE * HIDDEN_SIZE);
    biases_.resize(2);
    
    // Xavier 初始化
    auto xavier = [](size_t fan_in, size_t fan_out) {
        return std::sqrt(2.0f / (fan_in + fan_out));
    };
    
    // 层1: input(8) → hidden(4)
    float scale1 = xavier(INPUT_SIZE, HIDDEN_SIZE);
    for (auto& w : weights_) {
        w = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale1;
    }
    
    // 层2: hidden(4) → output(1) - 在 weights_ 末尾
    float scale2 = xavier(HIDDEN_SIZE, OUTPUT_SIZE);
    for (size_t i = HIDDEN_SIZE * INPUT_SIZE; i < weights_.size(); i++) {
        weights_[i] = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale2;
    }
    
    // 偏置
    biases_[0].resize(HIDDEN_SIZE, 0.0f);
    biases_[1].resize(OUTPUT_SIZE, 0.0f);
    
    activations_.resize(2);
}

float NeuralPredictor::predict(const LoadFeature& features) noexcept {
    // 归一化输入
    float input[INPUT_SIZE] = {
        features.cpu_util / 1024.0f,
        features.run_queue_len / 32.0f,
        features.wakeups_100ms / 100.0f,
        features.frame_interval_us / 20000.0f,
        features.touch_rate_100ms / 20.0f,
        (features.thermal_margin + 30.0f) / 60.0f,
        features.battery_level / 100.0f,
        features.is_gaming ? 1.0f : 0.0f
    };
    
    // 前向传播: 输入 → 隐藏层 (ReLU)
    activations_[0].resize(HIDDEN_SIZE);
    for (size_t h = 0; h < HIDDEN_SIZE; h++) {
        float sum = biases_[0][h];
        for (size_t i = 0; i < INPUT_SIZE; i++) {
            sum += weights_[h * INPUT_SIZE + i] * input[i];
        }
        // ReLU 激活
        activations_[0][h] = std::max(0.0f, sum);
    }
    
    // 前向传播: 隐藏层 → 输出 (线性)
    activations_[1].resize(OUTPUT_SIZE);
    for (size_t o = 0; o < OUTPUT_SIZE; o++) {
        float sum = biases_[1][o];
        for (size_t h = 0; h < HIDDEN_SIZE; h++) {
            sum += weights_[HIDDEN_SIZE * INPUT_SIZE + o * HIDDEN_SIZE + h] * activations_[0][h];
        }
        activations_[1][o] = sum;
    }
    
    return std::clamp(activations_[1][0], 0.0f, 144.0f);
}

void NeuralPredictor::train(const LoadFeature& features, float actual_fps) noexcept {
    // 前向传播
    float pred = predict(features);
    float error = actual_fps - pred;
    
    // 反向传播 (简化版 SGD)
    // 输出层梯度: error * 1 (线性激活)
    float output_grad = error;
    
    // 隐藏层梯度
    float hidden_grad[HIDDEN_SIZE];
    for (size_t h = 0; h < HIDDEN_SIZE; h++) {
        float d_relu = activations_[0][h] > 0.0f ? 1.0f : 0.0f;
        float grad = 0.0f;
        for (size_t o = 0; o < OUTPUT_SIZE; o++) {
            grad += weights_[HIDDEN_SIZE * INPUT_SIZE + o * HIDDEN_SIZE + h] * output_grad;
        }
        hidden_grad[h] = grad * d_relu;
    }
    
    // 更新输出层权重
    for (size_t o = 0; o < OUTPUT_SIZE; o++) {
        for (size_t h = 0; h < HIDDEN_SIZE; h++) {
            weights_[HIDDEN_SIZE * INPUT_SIZE + o * HIDDEN_SIZE + h] += lr_ * output_grad * activations_[0][h];
        }
        biases_[1][o] += lr_ * output_grad;
    }
    
    // 更新隐藏层权重
    float input[INPUT_SIZE] = {
        features.cpu_util / 1024.0f,
        features.run_queue_len / 32.0f,
        features.wakeups_100ms / 100.0f,
        features.frame_interval_us / 20000.0f,
        features.touch_rate_100ms / 20.0f,
        (features.thermal_margin + 30.0f) / 60.0f,
        features.battery_level / 100.0f,
        features.is_gaming ? 1.0f : 0.0f
    };
    
    for (size_t h = 0; h < HIDDEN_SIZE; h++) {
        for (size_t i = 0; i < INPUT_SIZE; i++) {
            weights_[h * INPUT_SIZE + i] += lr_ * hidden_grad[h] * input[i];
        }
        biases_[0][h] += lr_ * hidden_grad[h];
    }
}

void NeuralPredictor::set_weights(
    const std::vector<std::vector<std::vector<float>>>& w,
    const std::vector<std::vector<float>>& b) noexcept {
    
    if (w.size() >= 2 && b.size() >= 2) {
        // 层1: 4×8 -> 扁平化 32
        size_t idx = 0;
        for (const auto& row : w[0]) {
            for (float val : row) {
                if (idx < HIDDEN_SIZE * INPUT_SIZE) {
                    weights_[idx] = val;
                    idx++;
                }
            }
        }
        
        // 层2: 1×4 -> 扁平化 4
        idx = HIDDEN_SIZE * INPUT_SIZE;
        for (const auto& row : w[1]) {
            for (float val : row) {
                if (idx < weights_.size()) {
                    weights_[idx] = val;
                    idx++;
                }
            }
        }
        
        // 偏置
        if (b[0].size() >= HIDDEN_SIZE) {
            biases_[0] = b[0];
        }
        if (b[1].size() >= OUTPUT_SIZE) {
            biases_[1] = b[1];
        }
    }
}

void NeuralPredictor::reset() noexcept {
    float scale1 = std::sqrt(2.0f / (INPUT_SIZE + HIDDEN_SIZE));
    float scale2 = std::sqrt(2.0f / (HIDDEN_SIZE + OUTPUT_SIZE));
    
    for (auto& w : weights_) {
        w = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale1;
    }
    for (size_t i = HIDDEN_SIZE * INPUT_SIZE; i < weights_.size(); i++) {
        weights_[i] = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale2;
    }
    std::fill(biases_[0].begin(), biases_[0].end(), 0.0f);
    std::fill(biases_[1].begin(), biases_[1].end(), 0.0f);
}

// ============================================================
// Predictor 实现 (双模型)
// ============================================================

void Predictor::train(const LoadFeature& features, float actual_fps) noexcept {
    // 训练线性模型
    float util = static_cast<float>(features.cpu_util) / 1024.0f;
    float calc_fps = features.frame_interval_us > 0 ?
                     (1000000.0f / features.frame_interval_us) : actual_fps;
    float error = actual_fps - calc_fps;
    float lr = 0.05f;
    
    linear_weights_[0] += lr * error * util;
    linear_weights_[1] += lr * error * static_cast<float>(features.run_queue_len) / 32.0f;
    linear_weights_[2] += lr * error;
    
    // 限制权重
    linear_weights_[0] = std::clamp(linear_weights_[0], -2.0f, 2.0f);
    linear_weights_[1] = std::clamp(linear_weights_[1], -2.0f, 2.0f);
    linear_weights_[2] = std::clamp(linear_weights_[2], -50.0f, 50.0f);
    
    ema_error_ = ema_error_ * 0.9f + error * 0.1f;
    last_util_ = util;
    
    // 训练神经网络
    neural_.train(features, actual_fps);
}

float Predictor::predict(const LoadFeature& features) noexcept {
    if (active_model_ == Model::NEURAL) {
        return predict_neural(features);
    }
    return predict_linear(features);
}

float Predictor::predict_linear(const LoadFeature& features) noexcept {
    float util = static_cast<float>(features.cpu_util) / 1024.0f;
    float rq = static_cast<float>(features.run_queue_len) / 32.0f;
    
    float pred = linear_weights_[0] * util +
                 linear_weights_[1] * rq +
                 linear_weights_[2];
    
    // 趋势修正
    float trend = (util - last_util_) * 10.0f;
    pred += trend * 0.5f;
    last_util_ = util;
    
    return std::clamp(pred, 0.0f, 144.0f);
}

float Predictor::predict_neural(const LoadFeature& features) noexcept {
    return neural_.predict(features);
}

void Predictor::export_linear(float& w_util, float& w_rq, float& bias, float& ema_err) const noexcept {
    w_util = linear_weights_[0];
    w_rq = linear_weights_[1];
    bias = linear_weights_[2];
    ema_err = ema_error_;
}

void Predictor::export_model(float* weights, float* biases) const noexcept {
    const auto& nn_w = neural_.get_weights();
    const auto& nn_b = neural_.get_biases();
    
    // wh: 4×8 = 32 floats
    std::memcpy(weights, nn_w.data(), NeuralPredictor::HIDDEN_SIZE * NeuralPredictor::INPUT_SIZE * sizeof(float));
    // wo: 1×4 = 4 floats
    std::memcpy(weights + NeuralPredictor::HIDDEN_SIZE * NeuralPredictor::INPUT_SIZE, 
                nn_w.data() + NeuralPredictor::HIDDEN_SIZE * NeuralPredictor::INPUT_SIZE,
                NeuralPredictor::OUTPUT_SIZE * NeuralPredictor::HIDDEN_SIZE * sizeof(float));
    // bh: 4 floats
    std::memcpy(biases, nn_b[0].data(), NeuralPredictor::HIDDEN_SIZE * sizeof(float));
    // bo: 1 float
    biases[NeuralPredictor::HIDDEN_SIZE] = nn_b[1][0];
}

void Predictor::import_linear(float w_util, float w_rq, float bias, float ema_err) noexcept {
    linear_weights_[0] = w_util;
    linear_weights_[1] = w_rq;
    linear_weights_[2] = bias;
    ema_error_ = ema_err;
}

void Predictor::import_model(const std::vector<std::vector<std::vector<float>>>& nn_weights,
                              const std::vector<std::vector<float>>& nn_biases) noexcept {
    if (nn_weights.size() >= 2 && nn_biases.size() >= 2) {
        neural_.set_weights(nn_weights, nn_biases);
    }
}

} // namespace hp::predict
