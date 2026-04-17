#pragma once
#include "core/types.h"
#include <array>
#include <vector>

namespace hp::predict {

class NeuralPredictor {
public:
    static constexpr size_t INPUT_SIZE = 8;
    static constexpr size_t HIDDEN_SIZE = 4;
    static constexpr size_t OUTPUT_SIZE = 1;
    
private:
    // 权重: 扁平化存储 [wh(32), wo(4)]
    std::vector<float> weights_;
    // 偏置 [bh(4), bo(1)]
    std::vector<std::vector<float>> biases_;
    // 激活值
    std::vector<std::vector<float>> activations_;
    
    float lr_{0.01f};
    
public:
    NeuralPredictor() noexcept;
    
    void train(const LoadFeature& features, float actual_fps) noexcept;
    float predict(const LoadFeature& features) noexcept;
    
    // 获取/设置权重 (用于 WebUI 同步)
    const std::vector<float>& get_weights() const noexcept { return weights_; }
    const std::vector<std::vector<float>>& get_biases() const noexcept { return biases_; }
    void set_weights(const std::vector<std::vector<std::vector<float>>>& w,
                     const std::vector<std::vector<float>>& b) noexcept;
    
    void reset() noexcept;
};

class Predictor {
public:
    enum class Model { LINEAR, NEURAL };
    
private:
    // 线性回归参数
    std::array<float, 3> linear_weights_{0.3f, -0.1f, 55.0f};
    float ema_error_{2.5f};
    float last_util_{0.0f};
    
    // 神经网络
    NeuralPredictor neural_;
    
    Model active_model_{Model::LINEAR};
    
public:
    void train(const LoadFeature& features, float actual_fps) noexcept;
    float predict(const LoadFeature& features) noexcept;
    float predict_linear(const LoadFeature& features) noexcept;
    float predict_neural(const LoadFeature& features) noexcept;
    
    void set_model(Model m) noexcept { active_model_ = m; }
    Model get_model() const noexcept { return active_model_; }
    
    const NeuralPredictor& neural() const noexcept { return neural_; }
    NeuralPredictor& neural() noexcept { return neural_; }
    
    // 导出权重
    void export_linear(float& w_util, float& w_rq, float& bias, float& ema_err) const noexcept;
    void export_model(float* weights, float* biases) const noexcept;
    void import_linear(float w_util, float w_rq, float bias, float ema_err) noexcept;
    void import_model(const std::vector<std::vector<std::vector<float>>>& nn_weights,
                      const std::vector<std::vector<float>>& nn_biases) noexcept;
};

} // namespace hp::predict