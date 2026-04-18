#pragma once
/**
 * @file parallel_compute.h
 * @brief 并行计算辅助类
 *
 * 设计原则：
 * 1. 低开销 - 最小化并行化开销
 * 2. 细粒度 - 支持小任务的并行化
 * 3. SIMD友好 - 与SIMD优化配合
 * 4. 负载均衡 - 均匀分配计算任务
 */

#include <array>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cmath>
#include "load_aware_pool.h"
#include "task_decomposer.h"

namespace hp::parallel {

// =============================================================================
// 并行EMA计算
// =============================================================================

class ParallelEMACompute {
private:
    static constexpr size_t MAX_SCALES = 8;
    std::array<float, MAX_SCALES> values_{};
    std::array<float, MAX_SCALES> alphas_{};
    size_t count_{0};

public:
    ParallelEMACompute() = default;

    void init(size_t num_scales, const float* alphas) {
        count_ = std::min(num_scales, MAX_SCALES);
        for (size_t i = 0; i < count_; ++i) {
            alphas_[i] = alphas[i];
        }
    }

    // 串行更新（单值）
    void update_serial(float new_value) {
        for (size_t i = 0; i < count_; ++i) {
            values_[i] = values_[i] * (1.0f - alphas_[i]) + new_value * alphas_[i];
        }
    }

    // 并行更新（多值）
    void update_parallel(const float* new_values, size_t size) {
        if (size == 1) {
            update_serial(new_values[0]);
            return;
        }

        // 计算平均值
        float sum = 0.0f;
        for (size_t i = 0; i < size; ++i) {
            sum += new_values[i];
        }
        float avg = sum / size;

        // 并行更新所有尺度
        auto& decomposer = global_task_decomposer();
        decomposer.parallel_for(0, count_, [this, avg](size_t i) {
            values_[i] = values_[i] * (1.0f - alphas_[i]) + avg * alphas_[i];
        });
    }

    // SIMD优化的批量更新
    void update_simd(const float* new_values, size_t size) {
        if (size == 1) {
            update_serial(new_values[0]);
            return;
        }

        // 计算平均值
        float sum = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        // NEON优化
        size_t i = 0;
        float32x4_t sum_vec = vdupq_n_f32(0.0f);
        for (; i + 4 <= size; i += 4) {
            float32x4_t vec = vld1q_f32(new_values + i);
            sum_vec = vaddq_f32(sum_vec, vec);
        }
        // 提取结果
        sum = vgetq_lane_f32(sum_vec, 0) + vgetq_lane_f32(sum_vec, 1) +
              vgetq_lane_f32(sum_vec, 2) + vgetq_lane_f32(sum_vec, 3);
        // 处理剩余元素
        for (; i < size; ++i) {
            sum += new_values[i];
        }
#else
        // 通用实现
        for (size_t i = 0; i < size; ++i) {
            sum += new_values[i];
        }
#endif
        float avg = sum / size;

        // 并行更新所有尺度
        update_serial(avg);
    }

    float get(size_t scale = 0) const {
        return scale < count_ ? values_[scale] : values_[0];
    }

    const float* get_all() const { return values_.data(); }
    size_t count() const { return count_; }
};

// =============================================================================
// 并行神经网络推理
// =============================================================================

class ParallelNNInference {
private:
    struct LayerConfig {
        size_t input_size;
        size_t output_size;
        const float* weights;
        const float* biases;
    };

    std::vector<LayerConfig> layers_;
    std::vector<std::vector<float>> activations_;

public:
    ParallelNNInference() = default;

    // 添加层
    void add_layer(size_t input_size, size_t output_size,
                   const float* weights, const float* biases) {
        layers_.push_back({input_size, output_size, weights, biases});
        activations_.emplace_back(output_size);
    }

    // 并行前向传播
    std::vector<float> forward(const float* input) {
        if (layers_.empty()) return {};

        // 第一层
        const auto& layer0 = layers_[0];
        parallel_matmul(activations_[0].data(), input,
                       layer0.weights, layer0.biases,
                       1, layer0.output_size, layer0.input_size);

        // ReLU激活
        parallel_relu(activations_[0].data(), layer0.output_size);

        // 后续层
        for (size_t l = 1; l < layers_.size(); ++l) {
            const auto& layer = layers_[l];
            const auto& prev_activation = activations_[l - 1];

            parallel_matmul(activations_[l].data(), prev_activation.data(),
                           layer.weights, layer.biases,
                           1, layer.output_size, layer.input_size);

            parallel_relu(activations_[l].data(), layer.output_size);
        }

        return activations_.back();
    }

    // 并行矩阵乘法
    void parallel_matmul(float* output, const float* input,
                        const float* weights, const float* biases,
                        size_t batch_size, size_t output_size, size_t input_size) {
        auto& decomposer = global_task_decomposer();

        // 并行计算每个输出
        decomposer.parallel_for(0, output_size, [output, input, weights, biases,
                                                   batch_size, input_size](size_t i) {
            float sum = biases ? biases[i] : 0.0f;
            for (size_t j = 0; j < input_size; ++j) {
                sum += weights[i * input_size + j] * input[j];
            }
            output[i] = sum;
        });
    }

    // 并行ReLU激活
    void parallel_relu(float* data, size_t size) {
        auto& decomposer = global_task_decomposer();

        decomposer.parallel_for(0, size, [data](size_t i) {
            data[i] = std::max(0.0f, data[i]);
        });
    }
};

// =============================================================================
// 并行负载计算
// =============================================================================

class ParallelLoadCompute {
private:
    static constexpr size_t MAX_CORES = 8;

public:
    // 并行计算所有核心的负载趋势
    static void compute_all_trends(const std::array<uint32_t, MAX_CORES>& utils,
                                   const std::array<uint64_t, MAX_CORES>& timestamps,
                                   std::array<float, MAX_CORES>& trends) {
        auto& decomposer = global_task_decomposer();

        decomposer.parallel_for(0, MAX_CORES, [&utils, &timestamps, &trends](size_t i) {
            // 简化计算：使用当前util作为趋势
            trends[i] = static_cast<float>(utils[i]) / 1024.0f;
        });
    }

    // 并行计算负载分布
    static float compute_load_distribution(const std::array<uint32_t, MAX_CORES>& utils) {
        auto& decomposer = global_task_decomposer();

        // 并行计算总和
        uint32_t total_util = decomposer.parallel_reduce(
            0, MAX_CORES, 0u,
            [&utils](uint32_t acc, size_t i) {
                return acc + utils[i];
            },
            [](uint32_t a, uint32_t b) { return a + b; }
        );

        // 计算平均值
        float avg = static_cast<float>(total_util) / MAX_CORES;

        // 并行计算方差
        float variance = decomposer.parallel_reduce(
            0, MAX_CORES, 0.0f,
            [&utils, avg](float acc, size_t i) {
                float diff = static_cast<float>(utils[i]) - avg;
                return acc + diff * diff;
            },
            [](float a, float b) { return a + b; }
        );

        variance /= MAX_CORES;

        // 返回变异系数
        return std::sqrt(variance) / (avg + 1.0f);
    }

    // 并行查找最优目标核心
    static int find_best_target(const std::array<uint32_t, MAX_CORES>& utils,
                               const std::array<uint32_t, MAX_CORES>& run_queues,
                               const std::array<float, MAX_CORES>& trends,
                               int current_cpu) {
        auto& decomposer = global_task_decomposer();

        // 并行计算所有核心的评分
        std::array<uint32_t, MAX_CORES> scores;
        decomposer.parallel_for(0, MAX_CORES, [&utils, &run_queues, &trends,
                                                &scores](size_t i) {
            // 综合评分：负载越低、运行队列越短、趋势越稳定，分数越高
            uint32_t load_score = 1024 - utils[i];
            uint32_t rq_score = (8 - run_queues[i]) * 64;
            uint32_t trend_score = static_cast<uint32_t>(
                std::max(0.0f, 1.0f - std::abs(trends[i])) * 128
            );
            scores[i] = load_score + rq_score + trend_score;
        });

        // 查找最高分
        int best_cpu = current_cpu;
        uint32_t best_score = scores[current_cpu];

        for (int i = 0; i < static_cast<int>(MAX_CORES); ++i) {
            if (i != current_cpu && scores[i] > best_score) {
                best_score = scores[i];
                best_cpu = i;
            }
        }

        return best_cpu;
    }
};

// =============================================================================
// 并行频率映射
// =============================================================================

class ParallelFreqMap {
private:
    std::vector<uint32_t> freq_table_;
    uint32_t max_freq_{0};

public:
    ParallelFreqMap() = default;

    void init(uint32_t max_freq, const std::vector<uint32_t>& freq_steps) {
        max_freq_ = max_freq;
        freq_table_.resize(1024);  // util 0-1023

        // 并行预计算映射表
        auto& decomposer = global_task_decomposer();

        decomposer.parallel_for(0, 1024, [this, max_freq, &freq_steps](size_t util) {
            float util_norm = static_cast<float>(util) / 1024.0f;
            uint32_t target_freq = static_cast<uint32_t>(1.25f * max_freq * util_norm);

            // 二分查找最近的频点
            size_t idx = 0;
            if (!freq_steps.empty() && target_freq > 0) {
                auto it = std::lower_bound(freq_steps.begin(), freq_steps.end(), target_freq);
                idx = std::distance(freq_steps.begin(), it);
                if (idx >= freq_steps.size()) {
                    idx = freq_steps.size() - 1;
                }
            }

            freq_table_[util] = freq_steps.empty() ? 0 : freq_steps[idx];
        });
    }

    uint32_t get_freq(uint32_t util) const {
        if (freq_table_.empty()) return 0;
        uint32_t idx = std::min(util, 1023u);
        return freq_table_[idx];
    }

    const std::vector<uint32_t>& get_table() const { return freq_table_; }
};

// =============================================================================
// 并行统计计算
// =============================================================================

class ParallelStats {
public:
    // 并行计算均值
    static float mean(const float* data, size_t size) {
        auto& decomposer = global_task_decomposer();

        return decomposer.parallel_reduce(
            0, size, 0.0f,
            [data](float acc, size_t i) { return acc + data[i]; },
            [](float a, float b) { return a + b; }
        ) / size;
    }

    // 并行计算方差
    static float variance(const float* data, size_t size, float mean) {
        auto& decomposer = global_task_decomposer();

        return decomposer.parallel_reduce(
            0, size, 0.0f,
            [data, mean](float acc, size_t i) {
                float diff = data[i] - mean;
                return acc + diff * diff;
            },
            [](float a, float b) { return a + b; }
        ) / size;
    }

    // 并行计算标准差
    static float stddev(const float* data, size_t size, float mean) {
        return std::sqrt(variance(data, size, mean));
    }

    // 并行计算最小值
    static float min(const float* data, size_t size) {
        auto& decomposer = global_task_decomposer();

        return decomposer.parallel_reduce(
            0, size, data[0],
            [data](float acc, size_t i) { return std::min(acc, data[i]); },
            [](float a, float b) { return std::min(a, b); }
        );
    }

    // 并行计算最大值
    static float max(const float* data, size_t size) {
        auto& decomposer = global_task_decomposer();

        return decomposer.parallel_reduce(
            0, size, data[0],
            [data](float acc, size_t i) { return std::max(acc, data[i]); },
            [](float a, float b) { return std::max(a, b); }
        );
    }
};

} // namespace hp::parallel
