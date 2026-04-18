#pragma once
/**
 * @file parallel_predictor.h
 * @brief 并行化预测器
 *
 * 设计原则：
 * 1. 低开销 - 最小化并行化开销
 * 2. 细粒度 - 支持小任务的并行化
 * 3. SIMD友好 - 与SIMD优化配合
 * 4. 负载均衡 - 均匀分配计算任务
 */

#include "predict/predictor.h"
#include "core/parallel_compute.h"
#include "core/load_aware_pool.h"
#include "core/task_decomposer.h"
#include <array>
#include <vector>
#include <atomic>

namespace hp::predict {

// =============================================================================
// 并行化预测器
// =============================================================================

class ParallelPredictor {
private:
    Predictor base_predictor_;
    parallel::ParallelEMACompute ema_compute_;
    parallel::ParallelNNInference nn_inference_;
    parallel::ParallelFreqMap freq_map_;

    // 并行计算状态
    struct ParallelState {
        std::array<float, 8> util_ema_{};
        std::array<float, 8> fps_ema_{};
        std::array<float, 8> util_trends_{};
        std::atomic<bool> computing{false};
    } parallel_state_;

public:
    ParallelPredictor() = default;

    // 初始化
    void init() {
        // 初始化EMA计算器
        float alphas[4] = {0.7f, 0.3f, 0.1f, 0.05f};
        ema_compute_.init(4, alphas);

        // 初始化神经网络推理器
        // 8→16→8→1
        const float* weights = get_pretrained_weights();
        const float* biases = get_pretrained_biases();

        // 层1: 8→16
        nn_inference_.add_layer(8, 16, weights, biases);
        // 层2: 16→8
        nn_inference_.add_layer(16, 8, weights + 128, biases + 16);
        // 层3: 8→1
        nn_inference_.add_layer(8, 1, weights + 256, biases + 24);
    }

    // 并行更新多时间尺度特征
    void update_multiscale_features_parallel(const LoadFeature& f, uint64_t now_ns) noexcept {
        if (parallel_state_.computing.load()) {
            // 如果正在计算，跳过
            return;
        }

        parallel_state_.computing.store(true);

        // 并行计算EMA
        float util = static_cast<float>(f.cpu_util) / 1024.0f;
        float fps = f.frame_interval_us > 0 ? 1000000.0f / f.frame_interval_us : 60.0f;

        // 并行更新util EMA
        ema_compute_.update_serial(util);
        parallel_state_.util_ema_[0] = ema_compute_.get(0);
        parallel_state_.util_ema_[1] = ema_compute_.get(1);
        parallel_state_.util_ema_[2] = ema_compute_.get(2);
        parallel_state_.util_ema_[3] = ema_compute_.get(3);

        // 并行更新fps EMA
        ema_compute_.update_serial(fps);
        parallel_state_.fps_ema_[0] = ema_compute_.get(0);
        parallel_state_.fps_ema_[1] = ema_compute_.get(1);
        parallel_state_.fps_ema_[2] = ema_compute_.get(2);

        // 并行计算趋势
        auto& decomposer = parallel::global_task_decomposer();
        decomposer.parallel_for(0, 4, [this, util](size_t i) {
            if (i > 0) {
                parallel_state_.util_trends_[i] =
                    (parallel_state_.util_ema_[i] - parallel_state_.util_ema_[i-1]) * 20.0f;
            }
        });

        parallel_state_.computing.store(false);
    }

    // 并行预测
    float predict_parallel(const LoadFeature& features) noexcept {
        // 归一化输入
        float input[8] = {
            features.cpu_util / 1024.0f,
            features.run_queue_len / 32.0f,
            features.wakeups_100ms / 100.0f,
            features.frame_interval_us / 20000.0f,
            features.touch_rate_100ms / 20.0f,
            (features.thermal_margin + 30.0f) / 60.0f,
            features.battery_level / 100.0f,
            features.is_gaming ? 1.0f : 0.0f
        };

        // 并行神经网络推理
        auto output = nn_inference_.forward(input);

        return std::clamp(output[0], 0.0f, 144.0f);
    }

    // 并行训练
    void train_parallel(const LoadFeature& features, float actual_fps) noexcept {
        // 使用异步训练
        base_predictor_.train_async(features, actual_fps);
    }

    // 获取当前场景
    SchedScene get_current_scene() const noexcept {
        return base_predictor_.get_current_scene();
    }

    // 获取IO boost
    uint32_t get_io_boost() const noexcept {
        return base_predictor_.get_io_boost();
    }

    // IO boost管理器
    IoWaitBoostManager& io_wait_manager() noexcept {
        return base_predictor_.io_wait_manager();
    }

    const IoWaitBoostManager& io_wait_manager() const noexcept {
        return base_predictor_.io_wait_manager();
    }

    // 模型切换
    void set_model(Predictor::Model m) noexcept {
        base_predictor_.set_model(m);
    }

    Predictor::Model get_model() const noexcept {
        return base_predictor_.get_model();
    }

    // 导出/导入权重
    void export_linear(float& w_util, float& w_rq, float& bias, float& ema_err) const noexcept {
        base_predictor_.export_linear(w_util, w_rq, bias, ema_err);
    }

    void export_model(float* weights, float* biases) const noexcept {
        base_predictor_.export_model(weights, biases);
    }

    void import_linear(float w_util, float w_rq, float bias, float ema_err) noexcept {
        base_predictor_.import_linear(w_util, w_rq, bias, ema_err);
    }

    void import_model(const std::vector<std::vector<std::vector<float>>>& nn_weights,
                      const std::vector<std::vector<float>>& nn_biases) noexcept {
        base_predictor_.import_model(nn_weights, nn_biases);
    }

private:
    // 获取预训练权重
    static const float* get_pretrained_weights() noexcept {
        static const float weights[] = {
            // 层1: 8→16
            0.45f, -0.12f, 0.08f, -0.05f, 0.15f, 0.10f, 0.02f, 0.03f,
            0.38f, -0.08f, 0.12f, -0.03f, 0.18f, 0.08f, 0.01f, 0.02f,
            0.52f, -0.15f, 0.05f, -0.08f, 0.12f, 0.12f, 0.03f, 0.04f,
            0.30f, -0.05f, 0.18f, -0.02f, 0.22f, 0.05f, 0.01f, 0.01f,
            -0.05f, 0.55f, 0.10f, 0.08f, 0.03f, -0.02f, 0.01f, -0.01f,
            -0.03f, 0.48f, 0.08f, 0.06f, 0.05f, -0.01f, 0.02f, -0.02f,
            -0.08f, 0.62f, 0.12f, 0.10f, 0.02f, -0.03f, 0.01f, -0.01f,
            -0.02f, 0.42f, 0.06f, 0.04f, 0.08f, 0.00f, 0.03f, -0.03f,
            0.10f, 0.08f, 0.60f, 0.15f, 0.05f, 0.03f, 0.02f, 0.01f,
            0.08f, 0.05f, 0.52f, 0.12f, 0.07f, 0.02f, 0.01f, 0.02f,
            0.15f, 0.10f, 0.68f, 0.18f, 0.03f, 0.04f, 0.02f, 0.00f,
            0.05f, 0.03f, 0.45f, 0.08f, 0.10f, 0.01f, 0.01f, 0.03f,
            0.20f, 0.05f, 0.08f, 0.70f, 0.12f, 0.02f, 0.01f, 0.05f,
            0.18f, 0.03f, 0.10f, 0.65f, 0.15f, 0.01f, 0.02f, 0.04f,
            0.25f, 0.08f, 0.05f, 0.75f, 0.10f, 0.03f, 0.01f, 0.03f,
            0.12f, 0.02f, 0.15f, 0.58f, 0.20f, 0.00f, 0.01f, 0.06f,
            // 层2: 16→8
            0.30f, 0.25f, -0.10f, 0.15f, 0.20f, 0.05f, 0.12f, 0.08f,
            0.25f, 0.30f, -0.08f, 0.12f, 0.18f, 0.03f, 0.10f, 0.05f,
            0.35f, 0.20f, -0.12f, 0.18f, 0.15f, 0.08f, 0.08f, 0.10f,
            0.20f, 0.35f, -0.05f, 0.10f, 0.22f, 0.02f, 0.15f, 0.03f,
            0.28f, 0.28f, -0.07f, 0.14f, 0.19f, 0.04f, 0.12f, 0.06f,
            0.32f, 0.22f, -0.10f, 0.16f, 0.17f, 0.06f, 0.09f, 0.08f,
            0.22f, 0.32f, -0.06f, 0.11f, 0.21f, 0.01f, 0.14f, 0.04f,
            0.26f, 0.26f, -0.08f, 0.13f, 0.20f, 0.03f, 0.11f, 0.07f,
            // 层3: 8→1
            0.40f, 0.35f, 0.25f, 0.30f, 0.20f, 0.15f, 0.18f, 0.12f
        };
        return weights;
    }

    static const float* get_pretrained_biases() noexcept {
        static const float biases[] = {
            // hidden1偏置 (16)
            0.5f, -0.3f, 0.8f, -0.2f, 0.3f, -0.1f, 0.6f, -0.4f,
            0.4f, -0.2f, 0.7f, -0.3f, 0.2f, 0.0f, 0.5f, -0.1f,
            // hidden2偏置 (8)
            0.3f, -0.2f, 0.5f, 0.1f, 0.4f, -0.1f, 0.2f, 0.0f,
            // output偏置
            60.0f
        };
        return biases;
    }
};

} // namespace hp::predict
