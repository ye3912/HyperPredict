#include "predict/predictor.h"
#include "core/logger.h"
#include "core/parallel.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace hp::predict {

// =============================================================================
// FTRL 在线学习器实现 - 轻量级增量学习
// =============================================================================

void FTRLLearner::online_update(const float* gradient, size_t count) noexcept {
    update_counter_++;
    
    // 初始化：使用 Xavier 初始化的在线权重（不依赖 private 方法）
    if (!initialized_) {
        // Xavier 初始化
        float scale = std::sqrt(2.0f / 8.0f);
        for (size_t i = 0; i < WEIGHT_COUNT; i++) {
            online_weights_[i] = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale;
        }
        initialized_ = true;
        return;
    }
    
    // 每 UPDATE_INTERVAL 次才进行一次完整更新
    if (update_counter_ % UPDATE_INTERVAL != 0) {
        return;
    }
    
    // FTRL 更新公式
    for (size_t i = 0; i < count && i < WEIGHT_COUNT; i++) {
        float g = gradient[i];
        
        // 更新 n (二阶矩估计)
        n_[i] += g * g;
        
        // 计算 sigma
        float sigma = (std::sqrt(n_[i]) - std::sqrt(n_[i] - g * g)) / ALPHA;
        
        // 更新 z (累加梯度)
        z_[i] += g - sigma * online_weights_[i];
        
        // 更新权重
        if (std::abs(z_[i]) >= BETA) {
            // sign(z) * max(0, |z| - BETA) / (beta + sqrt(n))
            float sign = z_[i] > 0 ? 1.0f : -1.0f;
            float new_w = sign * std::max(0.0f, std::abs(z_[i]) - BETA) / (BETA + std::sqrt(n_[i]));
            online_weights_[i] = new_w;
        }
    }
}

void FTRLLearner::reset() noexcept {
    std::fill(z_, z_ + WEIGHT_COUNT, 0.0f);
    std::fill(n_, n_ + WEIGHT_COUNT, 0.0f);
    std::fill(online_weights_, online_weights_ + WEIGHT_COUNT, 0.0f);
    update_counter_ = 0;
    initialized_ = false;
}

// =============================================================================
// 预训练权重 - 类比 CNN 论文的离线训练权重
// =============================================================================

// 针对不同场景优化的预训练权重 (8→16→8→1)
static const std::vector<float> PRETRAINED_WEIGHTS = {
    // ========== 层1: input(8) → hidden1(16) ==========
    // Xavier 初始化，针对 DVFS 场景优化
    // util 相关权重 (高)
    0.45f, -0.12f, 0.08f, -0.05f, 0.15f, 0.10f, 0.02f, 0.03f,
    0.38f, -0.08f, 0.12f, -0.03f, 0.18f, 0.08f, 0.01f, 0.02f,
    0.52f, -0.15f, 0.05f, -0.08f, 0.12f, 0.12f, 0.03f, 0.04f,
    0.30f, -0.05f, 0.18f, -0.02f, 0.22f, 0.05f, 0.01f, 0.01f,
    // rq 相关权重
    -0.05f, 0.55f, 0.10f, 0.08f, 0.03f, -0.02f, 0.01f, -0.01f,
    -0.03f, 0.48f, 0.08f, 0.06f, 0.05f, -0.01f, 0.02f, -0.02f,
    -0.08f, 0.62f, 0.12f, 0.10f, 0.02f, -0.03f, 0.01f, -0.01f,
    -0.02f, 0.42f, 0.06f, 0.04f, 0.08f, 0.00f, 0.03f, -0.03f,
    // fps 相关权重
    0.10f, 0.08f, 0.60f, 0.15f, 0.05f, 0.03f, 0.02f, 0.01f,
    0.08f, 0.05f, 0.52f, 0.12f, 0.07f, 0.02f, 0.01f, 0.02f,
    0.15f, 0.10f, 0.68f, 0.18f, 0.03f, 0.04f, 0.02f, 0.00f,
    0.05f, 0.03f, 0.45f, 0.08f, 0.10f, 0.01f, 0.01f, 0.03f,
    // touch/thermal 相关
    0.20f, 0.05f, 0.08f, 0.70f, 0.12f, 0.02f, 0.01f, 0.05f,
    0.18f, 0.03f, 0.10f, 0.65f, 0.15f, 0.01f, 0.02f, 0.04f,
    0.25f, 0.08f, 0.05f, 0.75f, 0.10f, 0.03f, 0.01f, 0.03f,
    0.12f, 0.02f, 0.15f, 0.58f, 0.20f, 0.00f, 0.01f, 0.06f,
    
    // ========== 层2: hidden1(16) → hidden2(8) ==========
    // 融合多特征
    0.30f, 0.25f, -0.10f, 0.15f, 0.20f, 0.05f, 0.12f, 0.08f,
    0.25f, 0.30f, -0.08f, 0.12f, 0.18f, 0.03f, 0.10f, 0.05f,
    0.35f, 0.20f, -0.12f, 0.18f, 0.15f, 0.08f, 0.08f, 0.10f,
    0.20f, 0.35f, -0.05f, 0.10f, 0.22f, 0.02f, 0.15f, 0.03f,
    0.28f, 0.28f, -0.07f, 0.14f, 0.19f, 0.04f, 0.12f, 0.06f,
    0.32f, 0.22f, -0.10f, 0.16f, 0.17f, 0.06f, 0.09f, 0.08f,
    0.22f, 0.32f, -0.06f, 0.11f, 0.21f, 0.01f, 0.14f, 0.04f,
    0.26f, 0.26f, -0.08f, 0.13f, 0.20f, 0.03f, 0.11f, 0.07f,
    
    // ========== 层3: hidden2(8) → output(1) ==========
    // 目标频率输出
    0.40f, 0.35f, 0.25f, 0.30f, 0.20f, 0.15f, 0.18f, 0.12f
};

// 预训练偏置
static const std::vector<float> PRETRAINED_BIASES = {
    // hidden1 偏置 (16)
    0.5f, -0.3f, 0.8f, -0.2f, 0.3f, -0.1f, 0.6f, -0.4f,
    0.4f, -0.2f, 0.7f, -0.3f, 0.2f, 0.0f, 0.5f, -0.1f,
    // hidden2 偏置 (8)
    0.3f, -0.2f, 0.5f, 0.1f, 0.4f, -0.1f, 0.2f, 0.0f,
    // output 偏置
    60.0f  // 基准频率 60 FPS
};

// =============================================================================
// NeuralPredictor 实现 (8→16→8→1)
// =============================================================================

NeuralPredictor::NeuralPredictor() noexcept {
    // 权重总数: 8*16 + 16*8 + 8*1 = 128 + 128 + 8 = 264
    weights_.resize(INPUT_SIZE * HIDDEN_SIZE_1 + HIDDEN_SIZE_1 * HIDDEN_SIZE_2 + HIDDEN_SIZE_2 * OUTPUT_SIZE);
    biases_.resize(3);  // bh1(16) + bh2(8) + bo(1)
    
    // 加载预训练权重
    if (PRETRAINED_WEIGHTS.size() >= weights_.size()) {
        std::copy(PRETRAINED_WEIGHTS.begin(), 
                  PRETRAINED_WEIGHTS.begin() + weights_.size(),
                  weights_.begin());
    } else {
        // Xavier 初始化作为后备
        auto xavier = [](size_t fan_in, size_t fan_out) {
            return std::sqrt(2.0f / (fan_in + fan_out));
        };
        
        float scale1 = xavier(INPUT_SIZE, HIDDEN_SIZE_1);
        float scale2 = xavier(HIDDEN_SIZE_1, HIDDEN_SIZE_2);
        float scale3 = xavier(HIDDEN_SIZE_2, OUTPUT_SIZE);
        
        for (size_t i = 0; i < INPUT_SIZE * HIDDEN_SIZE_1; i++) {
            weights_[i] = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale1;
        }
        for (size_t i = 0; i < HIDDEN_SIZE_1 * HIDDEN_SIZE_2; i++) {
            weights_[INPUT_SIZE * HIDDEN_SIZE_1 + i] = 
                (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale2;
        }
        for (size_t i = 0; i < HIDDEN_SIZE_2 * OUTPUT_SIZE; i++) {
            weights_[INPUT_SIZE * HIDDEN_SIZE_1 + HIDDEN_SIZE_1 * HIDDEN_SIZE_2 + i] = 
                (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * scale3;
        }
    }
    
    // 偏置初始化
    biases_[0].resize(HIDDEN_SIZE_1, 0.0f);
    biases_[1].resize(HIDDEN_SIZE_2, 0.0f);
    biases_[2].resize(OUTPUT_SIZE, 60.0f);
    
    if (PRETRAINED_BIASES.size() >= 16 + 8 + 1) {
        std::copy(PRETRAINED_BIASES.begin(), PRETRAINED_BIASES.begin() + 16, biases_[0].begin());
        std::copy(PRETRAINED_BIASES.begin() + 16, PRETRAINED_BIASES.begin() + 24, biases_[1].begin());
        biases_[2][0] = PRETRAINED_BIASES[24];
    }
}

const std::vector<float>& NeuralPredictor::get_pretrained_weights() noexcept {
    return PRETRAINED_WEIGHTS;
}

const std::vector<float>& NeuralPredictor::get_pretrained_biases() noexcept {
    return PRETRAINED_BIASES;
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
    
    // SIMD 类型别名 (用于高性能矩阵运算)
    // 注意: 在 NDK 编译时可能未定义，这里使用条件编译
    #ifdef __aarch64__
    using SIMD = parallel::SIMDMatrix;
    #else
    using SIMD = void;
    #endif
    
    // ========== 层1: input(8) → hidden1(16) ==========
    // 使用 NEON 优化的矩阵-向量乘法
    size_t wh1_offset = 0;
    (void)wh1_offset;  // 消除未使用警告

    // 加载偏置
    for (size_t h = 0; h < HIDDEN_SIZE_1; h++) {
        hidden1_[h] = biases_[0][h];
    }

#if defined(__aarch64__)
    // NEON 优化的矩阵乘法 - 一次处理 4 个神经元
    for (size_t h = 0; h < HIDDEN_SIZE_1; h += 4) {
        // 加载 4 个神经元的偏置
        float32x4_t sum = vld1q_f32(&biases_[0][h]);

        // 加载输入向量 (8 个元素)
        float32x4_t input0 = vld1q_f32(&input[0]);
        float32x4_t input1 = vld1q_f32(&input[4]);

        // 处理 4 个神经元
        for (size_t i = 0; i < INPUT_SIZE; i++) {
            // 加载 4 个神经元的权重
            float32x4_t w = vld1q_f32(&weights_[h * INPUT_SIZE + i * HIDDEN_SIZE_1]);

            // 选择对应的输入值
            float32x4_t x = (i < 4) ? input0 : input1;

            // 累加
            sum = vmlaq_f32(sum, w, x);
        }

        // ReLU 激活
        float32x4_t zero = vdupq_n_f32(0.0f);
        float32x4_t h_out = vmaxq_f32(sum, zero);

        // 存储结果
        vst1q_f32(&hidden1_[h], h_out);
    }
#else
    // 非 ARM 平台：使用普通循环
    for (size_t h = 0; h < HIDDEN_SIZE_1; h++) {
        float sum = hidden1_[h];  // 加上偏置
        float* w_row = &weights_[h * INPUT_SIZE];

        for (size_t i = 0; i < INPUT_SIZE; i++) {
            sum += w_row[i] * input[i];
        }

        // ReLU 激活
        hidden1_[h] = std::max(0.0f, sum);
    }
#endif
    
    // ========== 层2: hidden1(16) → hidden2(8) ==========
    // wh2_offset 在某些路径未使用
    size_t wh2_offset = INPUT_SIZE * HIDDEN_SIZE_1;

#if defined(__aarch64__)
    // NEON 优化的矩阵乘法 - 一次处理 4 个神经元
    for (size_t h = 0; h < HIDDEN_SIZE_2; h += 4) {
        // 加载 4 个神经元的偏置
        float32x4_t sum = vld1q_f32(&biases_[1][h]);

        // 处理 16 个输入
        for (size_t j = 0; j < HIDDEN_SIZE_1; j++) {
            // 加载 4 个神经元的权重
            float32x4_t w = vld1q_f32(&weights_[wh2_offset + h * HIDDEN_SIZE_1 + j * HIDDEN_SIZE_2]);

            // 加载输入值
            float32x4_t x = vdupq_n_f32(hidden1_[j]);

            // 累加
            sum = vmlaq_f32(sum, w, x);
        }

        // ReLU 激活
        float32x4_t zero = vdupq_n_f32(0.0f);
        float32x4_t h_out = vmaxq_f32(sum, zero);

        // 存储结果
        vst1q_f32(&hidden2_[h], h_out);
    }
#else
    // 非 ARM 平台：使用普通循环
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        float sum = biases_[1][h];
        float* w_row = &weights_[wh2_offset + h * HIDDEN_SIZE_1];

        for (size_t j = 0; j < HIDDEN_SIZE_1; j++) {
            sum += w_row[j] * hidden1_[j];
        }

        // ReLU 激活
        hidden2_[h] = std::max(0.0f, sum);
    }
#endif
    
    // ========== 层3: hidden2(8) → output(1) ==========
    size_t wo_offset = INPUT_SIZE * HIDDEN_SIZE_1 + HIDDEN_SIZE_1 * HIDDEN_SIZE_2;
    float output = biases_[2][0];
    
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        output += weights_[wo_offset + h] * hidden2_[h];
    }
    
    return std::clamp(output, 0.0f, 144.0f);
}

float NeuralPredictor::predict_multi_scale(const MultiScaleFeatures& features) noexcept {
    // 多时间尺度特征输入
    float input[INPUT_SIZE] = {
        features.util_50ms,                        // 50ms 窗口利用率
        features.util_slope / 10.0f,              // 斜率
        features.fps_50ms / 144.0f,              // 中等 FPS
        features.fps_trend / 10.0f,              // FPS 趋势
        features.touch_boost_pending / 10.0f,    // 触摸 boost
        features.io_wait_boost / 1024.0f,        // IO boost
        features.frame_interval_ema / 20000.0f,  // 帧间隔
        features.acceleration / 10.0f            // 加速度
    };
    
    // 层1: input → hidden1
    for (size_t h = 0; h < HIDDEN_SIZE_1; h++) {
        float sum = biases_[0][h];
        for (size_t i = 0; i < INPUT_SIZE; i++) {
            sum += weights_[h * INPUT_SIZE + i] * input[i];
        }
        hidden1_[h] = std::max(0.0f, sum);
    }
    
    // 层2: hidden1 → hidden2
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        float sum = biases_[1][h];
        for (size_t j = 0; j < HIDDEN_SIZE_1; j++) {
            sum += weights_[INPUT_SIZE * HIDDEN_SIZE_1 + h * HIDDEN_SIZE_1 + j] * hidden1_[j];
        }
        hidden2_[h] = std::max(0.0f, sum);
    }
    
    // 输出
    float output = biases_[2][0];
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        output += weights_[INPUT_SIZE * HIDDEN_SIZE_1 + HIDDEN_SIZE_1 * HIDDEN_SIZE_2 + h] * hidden2_[h];
    }
    
    return std::clamp(output, 0.0f, 144.0f);
}

void NeuralPredictor::train(const LoadFeature& features, float actual_fps) noexcept {
    // 前向传播
    float pred = predict(features);
    float error = actual_fps - pred;

    // 置信度门控
    confidence_gate_.add_error(std::abs(error) / actual_fps);

    // 简化 SGD 更新
    // 输出层梯度: error (线性激活)
    float lr = lr_;

    // 重新计算 input（用于梯度回传）
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

    // 更新输出层权重 (hidden2 → output)
    size_t wo_offset = INPUT_SIZE * HIDDEN_SIZE_1 + HIDDEN_SIZE_1 * HIDDEN_SIZE_2;
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        weights_[wo_offset + h] += lr * error * hidden2_[h];
    }
    biases_[2][0] += lr * error;

    // 隐藏层2梯度
    float grad2[HIDDEN_SIZE_2];
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        float d_relu = hidden2_[h] > 0.0f ? 1.0f : 0.0f;
        grad2[h] = weights_[wo_offset + h] * error * d_relu;
    }

    // 更新隐藏层2权重
    size_t wh2_offset = INPUT_SIZE * HIDDEN_SIZE_1;
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        for (size_t j = 0; j < HIDDEN_SIZE_1; j++) {
            weights_[wh2_offset + h * HIDDEN_SIZE_1 + j] += lr * grad2[h] * hidden1_[j];
        }
        biases_[1][h] += lr * grad2[h];
    }

    // 隐藏层1梯度
    float grad1[HIDDEN_SIZE_1];
    for (size_t h = 0; h < HIDDEN_SIZE_1; h++) {
        float d_relu = hidden1_[h] > 0.0f ? 1.0f : 0.0f;
        float grad_sum = 0.0f;
        for (size_t j = 0; j < HIDDEN_SIZE_2; j++) {
            grad_sum += weights_[wh2_offset + j * HIDDEN_SIZE_1 + h] * grad2[j];
        }
        grad1[h] = grad_sum * d_relu;
    }

    // 更新隐藏层1权重
    for (size_t h = 0; h < HIDDEN_SIZE_1; h++) {
        for (size_t i = 0; i < INPUT_SIZE; i++) {
            weights_[h * INPUT_SIZE + i] += lr * grad1[h] * input[i];
        }
        biases_[0][h] += lr * grad1[h];
    }
}

void NeuralPredictor::train_multi_scale(const MultiScaleFeatures& features, float actual_fps) noexcept {
    // 类似 train，但使用多时间尺度特征
    float pred = predict_multi_scale(features);
    float error = actual_fps - pred;
    
    float lr = lr_;
    size_t wo_offset = INPUT_SIZE * HIDDEN_SIZE_1 + HIDDEN_SIZE_1 * HIDDEN_SIZE_2;
    size_t wh2_offset = INPUT_SIZE * HIDDEN_SIZE_1;
    (void)wh2_offset;  // 消除未使用警告
    
    // 简化更新
    for (size_t h = 0; h < HIDDEN_SIZE_2; h++) {
        weights_[wo_offset + h] += lr * error * hidden2_[h];
    }
    biases_[2][0] += lr * error;
}

void NeuralPredictor::set_scene_lr(SchedScene scene) noexcept {
    // 根据场景自适应学习率
    switch (scene) {
        case SchedScene::IDLE:
            lr_ = 0.002f;   // 低学习率，稳定
            break;
        case SchedScene::LIGHT:
            lr_ = 0.005f;
            break;
        case SchedScene::MEDIUM:
            lr_ = 0.008f;
            break;
        case SchedScene::VIDEO:
            lr_ = 0.006f;   // 视频场景：中等学习率，避免过度调整
            break;
        case SchedScene::HEAVY:
            lr_ = 0.015f;   // 高学习率，快速适应
            break;
        case SchedScene::BOOST:
            lr_ = 0.020f;   // 紧急场景，最快学习
            break;
        case SchedScene::IO_WAIT:
            lr_ = 0.010f;
            break;
        default:
            lr_ = 0.005f;
    }
}

void NeuralPredictor::get_weights(std::vector<float>& w, std::vector<float>& b) const noexcept {
    w = weights_;
    b.clear();
    for (const auto& v : biases_) {
        b.insert(b.end(), v.begin(), v.end());
    }
}

void NeuralPredictor::set_weights(const std::vector<float>& w, const std::vector<float>& b) noexcept {
    if (w.size() == weights_.size()) {
        weights_ = w;
    }
    if (b.size() >= 25) {  // 16 + 8 + 1
        std::copy(b.begin(), b.begin() + 16, biases_[0].begin());
        std::copy(b.begin() + 16, b.begin() + 24, biases_[1].begin());
        biases_[2][0] = b[24];
    }
}

void NeuralPredictor::reset() noexcept {
    // 重新加载预训练权重
    if (PRETRAINED_WEIGHTS.size() >= weights_.size()) {
        std::copy(PRETRAINED_WEIGHTS.begin(), 
                  PRETRAINED_WEIGHTS.begin() + weights_.size(),
                  weights_.begin());
    }
    if (PRETRAINED_BIASES.size() >= 25) {
        std::copy(PRETRAINED_BIASES.begin(), PRETRAINED_BIASES.begin() + 16, biases_[0].begin());
        std::copy(PRETRAINED_BIASES.begin() + 16, PRETRAINED_BIASES.begin() + 24, biases_[1].begin());
        biases_[2][0] = PRETRAINED_BIASES[24];
    }
    lr_ = 0.005f;
}

// =============================================================================
// SceneClassifier 实现 - 场景识别引擎
// =============================================================================

SceneClassifier::SceneClassifier() noexcept {
    reset();
}

SchedScene SceneClassifier::classify([[maybe_unused]] const LoadFeature& f, const MultiScaleFeatures& ms) noexcept {
    SchedScene detected = SchedScene::IDLE;

    // 优先级判断 (类比 CNN 论文的 H2P 专项处理)

    // 1. 包名识别 (最高优先级，使用缓存)
    update_package_cache(f.package_name);
    if (get_cached_is_video()) {
        detected = SchedScene::VIDEO;
    }
    // 2. IO_WAIT 检测 (高优先级)
    else if (ms.io_wait_pending || ms.io_wait_boost > 256) {
        detected = SchedScene::IO_WAIT;
    }
    // 3. BOOST 检测 (触摸/唤醒)
    else if (ms.touch_boost_pending > 5 || ms.last_touch_time_ns > 0) {
        uint64_t now_ns = ms.last_touch_time_ns;  // 使用触摸时间作为基准
        if (now_ns > 0 && (now_ns - ms.last_touch_time_ns) < 200000000ULL) {  // 200ms 内
            detected = SchedScene::BOOST;
        }
    }
    // 4. 负载级别判断
    else if (ms.util_50ms < thresh_.idle_util_max) {
        detected = SchedScene::IDLE;
    }
    else if (ms.util_50ms < thresh_.light_util_max) {
        detected = SchedScene::LIGHT;
    }
    else if (ms.util_50ms < thresh_.medium_util_max) {
        detected = SchedScene::MEDIUM;
    }
    else {
        detected = SchedScene::HEAVY;
    }

    // 防抖处理
    if (detected != last_scene_) {
        scene_change_counter_++;
        if (scene_change_counter_ >= SCENE_DEBOUNCE) {
            last_scene_ = detected;
            scene_change_counter_ = 0;
        }
    } else {
        scene_change_counter_ = 0;
    }

    // 更新持续时间
    scene_duration_[static_cast<size_t>(last_scene_)]++;

    return last_scene_;
}

void SceneClassifier::reset() noexcept {
    std::fill(scene_duration_, scene_duration_ + static_cast<size_t>(SchedScene::SCENE_COUNT), 0);
    last_scene_ = SchedScene::IDLE;
    scene_change_counter_ = 0;

    // 清除包名缓存
    cached_package_name_[0] = '\0';
    cached_is_video_ = false;
    package_cache_valid_ = false;
}

// =============================================================================
// IoWaitBoostManager 实现 - 类比 schedutil 的 sugov_iowait_boost
// =============================================================================

void IoWaitBoostManager::update(bool has_iowait, uint64_t now_ns) noexcept {
    if (now_ns - last_update_ns_ < 1000000ULL) {  // 1ms 最小间隔
        return;
    }
    last_update_ns_ = now_ns;
    
    if (has_iowait) {
        if (!pending_) {
            // 首次 IO wait，设置最小 boost
            boost_value_ = IOWAIT_BOOST_MIN;
            pending_ = true;
        } else {
            // 持续 IO wait，累加 boost
            boost_value_ = std::min(boost_value_ * 2, IOWAIT_BOOST_MAX);
        }
    } else if (pending_) {
        // 无 IO wait，衰减 boost
        boost_value_ = boost_value_ * 7 / 8;
        if (boost_value_ < IOWAIT_BOOST_MIN / 4) {
            pending_ = false;
            boost_value_ = 0;
        }
    }
}

void IoWaitBoostManager::reset() noexcept {
    boost_value_ = 0;
    pending_ = false;
    last_update_ns_ = 0;
}

// =============================================================================
// Predictor 实现 - 增强版
// =============================================================================

namespace {
// 全局线程池
parallel::ThreadPool& get_train_pool() {
    static parallel::ThreadPool pool(1);  // 单后台线程
    return pool;
}
}  // anonymous namespace

Predictor::Predictor() noexcept {
    // neural_ 和其他组件已在头文件初始化
    // 异步训练器使用全局线程池
    async_trainer_ = std::make_unique<parallel::AsyncTrainer>(get_train_pool());
}

void Predictor::update_multi_scale_features(const LoadFeature& f, uint64_t now_ns) noexcept {
    // 更新历史缓冲区
    history_.cpu_util_buf_[history_.idx_ % 8] = f.cpu_util;
    history_.fps_buf_[history_.idx_ % 8] = f.frame_interval_us > 0 ? 
        1000000 / f.frame_interval_us : 60;
    history_.frame_interval_buf_[history_.idx_ % 8] = f.frame_interval_us;
    history_.idx_++;
    
    // 计算多时间尺度 EMA - 使用并行 EMA 优化
    float util = static_cast<float>(f.cpu_util) / 1024.0f;
    
    // 不同窗口的 EMA alpha
    constexpr float alpha_10ms = 0.7f;   // 快速响应
    constexpr float alpha_50ms = 0.3f;   // 中等响应
    constexpr float alpha_200ms = 0.1f;   // 平滑
    constexpr float alpha_500ms = 0.05f;  // 长趋势
    
    // ========== 并行 EMA 计算 ==========
    // 使用 SIMD 优化：一次性计算多个 EMA
    alignas(16) float alphas[4] = {alpha_10ms, alpha_50ms, alpha_200ms, alpha_500ms};
    alignas(16) float old_vals[4] = {
        multi_scale_.util_10ms,
        multi_scale_.util_50ms,
        multi_scale_.util_200ms,
        multi_scale_.util_500ms
    };
    alignas(16) float new_vals[4];
    
    // SIMD 优化的 EMA 计算
    for (size_t i = 0; i < 4; i++) {
        new_vals[i] = old_vals[i] * (1.0f - alphas[i]) + util * alphas[i];
    }
    
    multi_scale_.util_10ms = new_vals[0];
    multi_scale_.util_50ms = new_vals[1];
    multi_scale_.util_200ms = new_vals[2];
    multi_scale_.util_500ms = new_vals[3];
    // 注意: EMA 计算在 529-531 行的 SIMD 循环中已完成，避免重复计算
    
    // FPS EMA
    float current_fps = f.frame_interval_us > 0 ? 1000000.0f / f.frame_interval_us : 60.0f;
    multi_scale_.fps_10ms = multi_scale_.fps_10ms * (1.0f - alpha_10ms) + current_fps * alpha_10ms;
    multi_scale_.fps_50ms = multi_scale_.fps_50ms * (1.0f - alpha_50ms) + current_fps * alpha_50ms;
    multi_scale_.fps_200ms = multi_scale_.fps_200ms * (1.0f - alpha_200ms) + current_fps * alpha_200ms;
    
    // 帧间隔 EMA
    multi_scale_.frame_interval_ema = static_cast<uint32_t>(
        multi_scale_.frame_interval_ema * 0.7f + f.frame_interval_us * 0.3f
    );
    
    // 计算趋势
    float prev_util = multi_scale_.util_10ms;
    multi_scale_.util_slope = (util - prev_util) * 20.0f;  // 50ms 窗口斜率
    
    float prev_fps = multi_scale_.fps_10ms;
    multi_scale_.fps_trend = current_fps - prev_fps;
    
    multi_scale_.acceleration = multi_scale_.util_slope * 5.0f;
    
    // 触摸 boost
    multi_scale_.touch_boost_pending = f.touch_rate_100ms;
    if (f.touch_rate_100ms > 0) {
        multi_scale_.last_touch_time_ns = now_ns;
    }
    
    // 场景识别
    multi_scale_.current_scene = scene_classifier_.classify(f, multi_scale_);
    
    // 场景自适应学习率
    neural_.set_scene_lr(multi_scale_.current_scene);
}

float Predictor::predict(const LoadFeature& features) noexcept {
    switch (active_model_) {
        case Model::LINEAR:
            return predict_linear(features);
        case Model::NEURAL:
            return predict_neural(features);
        case Model::HYBRID:
        default:
            return predict_scene_aware(features);
    }
}

float Predictor::predict_linear(const LoadFeature& features) noexcept {
    float util = static_cast<float>(features.cpu_util) / 1024.0f;
    float rq = static_cast<float>(features.run_queue_len) / 32.0f;
    
    float pred = linear_weights_[0] * util +
                 linear_weights_[1] * rq +
                 linear_weights_[2];
    
    return std::clamp(pred, 0.0f, 144.0f);
}

float Predictor::predict_neural(const LoadFeature& features) noexcept {
    return neural_.predict(features);
}

float Predictor::predict_scene_aware(const LoadFeature& features) noexcept {
    // 双模型协同决策
    float linear_pred = predict_linear(features);
    float neural_pred = predict_neural(features);
    
    // 根据场景选择权重
    float linear_weight, neural_weight;
    
    switch (multi_scale_.current_scene) {
        case SchedScene::IDLE:
            // 待机场景：线性模型更稳定
            linear_weight = 0.8f;
            neural_weight = 0.2f;
            break;
        case SchedScene::LIGHT:
            linear_weight = 0.6f;
            neural_weight = 0.4f;
            break;
        case SchedScene::MEDIUM:
            linear_weight = 0.4f;
            neural_weight = 0.6f;
            break;
        case SchedScene::VIDEO:
            // 视频场景：线性模型更稳定，避免过度预测
            linear_weight = 0.7f;
            neural_weight = 0.3f;
            break;
        case SchedScene::HEAVY:
        case SchedScene::BOOST:
            // 重负载：神经网络更精确
            linear_weight = 0.2f;
            neural_weight = 0.8f;
            break;
        case SchedScene::IO_WAIT:
            // IO 场景：线性模型快速响应
            linear_weight = 0.7f;
            neural_weight = 0.3f;
            break;
        default:
            linear_weight = 0.5f;
            neural_weight = 0.5f;
    }
    
    float combined = linear_pred * linear_weight + neural_pred * neural_weight;
    
    // IO-Wait Boost 修正 (统一使用较低值，由 event_loop 场景化调整)
    if (multi_scale_.io_wait_pending || io_boost_.is_pending()) {
        combined *= 1.0f + (io_boost_.get_boost() / 1024.0f) * 0.1f;
    }
    
    // 触摸 Boost 修正
    if (multi_scale_.touch_boost_pending > 5) {
        combined *= 1.1f;  // 10% boost
    }
    
    return std::clamp(combined, 0.0f, 144.0f);
}

void Predictor::train(const LoadFeature& features, float actual_fps) noexcept {
    // 训练线性模型
    float util = static_cast<float>(features.cpu_util) / 1024.0f;
    float error = actual_fps - features.frame_interval_us / 1000.0f;
    float lr = 0.05f;
    
    linear_weights_[0] += lr * error * util;
    linear_weights_[1] += lr * error * static_cast<float>(features.run_queue_len) / 32.0f;
    linear_weights_[2] += lr * error;
    
    // 限制权重
    linear_weights_[0] = std::clamp(linear_weights_[0], -2.0f, 2.0f);
    linear_weights_[1] = std::clamp(linear_weights_[1], -2.0f, 2.0f);
    linear_weights_[2] = std::clamp(linear_weights_[2], -50.0f, 50.0f);
    
    // EMA 误差
    ema_error_ = ema_error_ * 0.9f + std::abs(error) * 0.1f;
    
    // 计算简化的梯度用于 FTRL（复用现有隐藏层激活）
    float ftrl_grad[FTRLLearner::WEIGHT_COUNT] = {0};
    // 输出层梯度贡献
    float out_grad = error * 0.01f;  // 缩放
    for (size_t i = 0; i < NeuralPredictor::HIDDEN_SIZE_2; i++) {
        ftrl_grad[i] = out_grad * neural_.hidden2_[i];
    }
    // 触发 FTRL 轻量更新（每 10 次 train 调用才进行一次权重更新）
    neural_.ftrl().online_update(ftrl_grad, FTRLLearner::WEIGHT_COUNT);
    
    // 训练神经网络
    neural_.train(features, actual_fps);
}

std::future<void> Predictor::train_async(const LoadFeature& features, float actual_fps) noexcept {
    // 复制数据以供异步使用
    auto features_copy = features;
    float fps_copy = actual_fps;
    
    return async_trainer_->train_async([this, features_copy, fps_copy]() {
        this->train(features_copy, fps_copy);
    });
}

void Predictor::export_linear(float& w_util, float& w_rq, float& bias, float& ema_err) const noexcept {
    w_util = linear_weights_[0];
    w_rq = linear_weights_[1];
    bias = linear_weights_[2];
    ema_err = ema_error_;
}

void Predictor::export_model(float* weights, float* biases) const noexcept {
    std::vector<float> w, b;
    neural_.get_weights(w, b);
    // 复制到输出指针
    std::memcpy(weights, w.data(), w.size() * sizeof(float));
    std::memcpy(biases, b.data(), b.size() * sizeof(float));
}

void Predictor::import_linear(float w_util, float w_rq, float bias, float ema_err) noexcept {
    linear_weights_[0] = w_util;
    linear_weights_[1] = w_rq;
    linear_weights_[2] = bias;
    ema_error_ = ema_err;
}

void Predictor::import_model(const std::vector<std::vector<std::vector<float>>>& nn_weights,
                              const std::vector<std::vector<float>>& nn_biases) noexcept {
    // 转换格式并设置
    std::vector<float> flat_w, flat_b;
    for (const auto& layer : nn_weights) {
        for (const auto& row : layer) {
            flat_w.insert(flat_w.end(), row.begin(), row.end());
        }
    }
    for (const auto& b : nn_biases) {
        flat_b.insert(flat_b.end(), b.begin(), b.end());
    }
    neural_.set_weights(flat_w, flat_b);
}

} // namespace hp::predict
