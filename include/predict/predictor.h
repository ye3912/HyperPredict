#pragma once
#include "core/types.h"
#include "core/parallel.h"
#include <array>
#include <vector>
#include <cstdint>

namespace hp::predict {

// =============================================================================
// 场景类型 - 类比 CNN 论文中针对 H2P 的专项优化
// =============================================================================
enum class SchedScene {
    IDLE        = 0,  // 待机
    LIGHT       = 1,  // 轻度负载 (浏览/社交)
    MEDIUM      = 2,  // 中度负载 (视频/音乐)
    HEAVY       = 3,  // 重度负载 (游戏)
    BOOST       = 4,  // 紧急 boost (触摸/唤醒)
    IO_WAIT     = 5,  // IO 密集型
    SCENE_COUNT = 6
};

// =============================================================================
// 多时间尺度特征历史 - 类比 CNN 的多粒度历史窗口
// =============================================================================
struct MultiScaleFeatures {
    // 不同时间尺度的利用率 EMA (类比 CNN 的多粒度历史)
    float util_10ms{0.0f};   // 10ms 窗口 - 快速响应
    float util_50ms{0.0f};   // 50ms 窗口 - 中等响应
    float util_200ms{0.0f};  // 200ms 窗口 - 平滑稳定
    float util_500ms{0.0f};  // 500ms 窗口 - 长趋势
    
    // FPS 特征
    float fps_10ms{60.0f};
    float fps_50ms{60.0f};
    float fps_200ms{60.0f};
    
    // 帧间隔特征
    uint32_t frame_interval_ema{16666};
    uint32_t frame_interval_var{0};  // 方差
    
    // 趋势特征 (类比 CNN 的路径历史)
    float util_slope{0.0f};         // 斜率
    float fps_trend{0.0f};          // FPS 趋势
    float acceleration{0.0f};       // 加速度
    
    // 触摸/唤醒状态
    uint32_t touch_boost_pending{0};    // 待 boost 的触摸次数
    uint64_t last_touch_time_ns{0};
    
    // IO-Wait 状态 (类比 schedutil)
    bool io_wait_pending{false};
    uint32_t io_wait_boost{0};
    uint64_t last_io_time_ns{0};
    
    // 当前识别到的场景
    SchedScene current_scene{SchedScene::IDLE};
};

// =============================================================================
// FTRL 在线学习器 - 轻量级增量学习
// 只增加 ~2KB 内存，每 10s 触发一次，CPU 开销极小
// =============================================================================
class FTRLLearner {
public:
    static constexpr size_t WEIGHT_COUNT = 264;  // 8*16 + 16*8 + 8*1
    static constexpr float ALPHA = 0.1f;         // 学习率
    static constexpr float BETA = 1.0f;         // L2 正则化强度
    static constexpr uint32_t UPDATE_INTERVAL = 10;  // 每 10 次触发一次

private:
    // FTRL 参数: z (累加梯度) + n (二阶矩估计)
    float z_[WEIGHT_COUNT] = {0};
    float n_[WEIGHT_COUNT] = {0};
    
    // 在线权重
    float online_weights_[WEIGHT_COUNT] = {0};
    
    // 更新计数器
    uint32_t update_counter_{0};
    bool initialized_{false};

public:
    // 增量更新（低频调用，开销可忽略）
    void online_update(const float* gradient, size_t count) noexcept;
    
    // 获取在线权重
    const float* get_weights() const noexcept { return online_weights_; }
    
    // 重置
    void reset() noexcept;
};

// =============================================================================
// 增强的 NeuralPredictor - 类比 BranchNet 架构
// 架构: 8→16→8→1 (从 8→4→1 升级)
// =============================================================================
class NeuralPredictor {
public:
    // 增强的架构
    static constexpr size_t INPUT_SIZE = 8;
    static constexpr size_t HIDDEN_SIZE_1 = 16;  // 增强: 4 → 16
    static constexpr size_t HIDDEN_SIZE_2 = 8;   // 新增第二层
    static constexpr size_t OUTPUT_SIZE = 1;
    
private:
    // 扁平化权重存储: wh1(8×16) + wh2(16×8) + wo(8×1) = 128 + 128 + 8 = 264
    std::vector<float> weights_;
    
    // 偏置: [bh1(16), bh2(8), bo(1)] - 改为二维向量
    std::vector<std::vector<float>> biases_;
    
    // FTRL 在线学习器
    FTRLLearner ftrl_;
    
    // 预计算的离线权重 (类比 CNN 论文的预训练权重)
    static const std::vector<float>& get_pretrained_weights() noexcept;
    static const std::vector<float>& get_pretrained_biases() noexcept;
    
    // 学习率 (场景自适应)
    float lr_{0.005f};
    
public:
    NeuralPredictor() noexcept;
    
    // 隐藏层激活缓存（用于 FTRL 梯度计算，public 让 Predictor 访问）
    float hidden1_[HIDDEN_SIZE_1];
    float hidden2_[HIDDEN_SIZE_2];
    
    // FTRL 在线学习器访问（public 供 Predictor 调用）
    const FTRLLearner& ftrl() const noexcept { return ftrl_; }
    FTRLLearner& ftrl() noexcept { return ftrl_; }
    
    // 多时间尺度特征输入的预测
    float predict(const LoadFeature& features) noexcept;
    float predict_multi_scale(const MultiScaleFeatures& features) noexcept;
    
    // 在线训练
    void train(const LoadFeature& features, float actual_fps) noexcept;
    void train_multi_scale(const MultiScaleFeatures& features, float actual_fps) noexcept;
    
    // 场景自适应的学习率
    void set_scene_lr(SchedScene scene) noexcept;
    
    // 权重导出/导入
    void get_weights(std::vector<float>& w, std::vector<float>& b) const noexcept;
    void set_weights(const std::vector<float>& w, const std::vector<float>& b) noexcept;
    
    void reset() noexcept;
};

// =============================================================================
// 场景识别引擎 - 类比 CNN 论文中针对不同分支类型的专项处理
// =============================================================================
class SceneClassifier {
private:
    // 各场景的持续时间统计
    uint32_t scene_duration_[static_cast<size_t>(SchedScene::SCENE_COUNT)]{0};
    SchedScene last_scene_{SchedScene::IDLE};
    
    // 场景切换计数器 (用于防抖)
    uint32_t scene_change_counter_{0};
    static constexpr uint32_t SCENE_DEBOUNCE = 5;  // 5次采样确认
    
    // 阈值配置 (可调)
    struct Thresholds {
        float idle_util_max{0.08f};
        float light_util_max{0.25f};
        float medium_util_max{0.50f};
        float heavy_util_max{0.75f};
        float boost_touch_min{30};  // 100ms 内触摸次数
        float io_wait_iowait_min{0.15f};
    } thresh_;
    
public:
    SceneClassifier() noexcept;
    
    // 识别当前场景
    SchedScene classify(const LoadFeature& f, const MultiScaleFeatures& ms) noexcept;
    
    // 获取场景持续时间
    uint32_t get_scene_duration(SchedScene s) const noexcept { 
        return scene_duration_[static_cast<size_t>(s)]; 
    }
    
    // 获取当前场景
    SchedScene get_current_scene() const noexcept { 
        return last_scene_; 
    }
    
    // 重置
    void reset() noexcept;
};

// =============================================================================
// IO-Wait Boost 管理器 - 类比 schedutil 的 sugov_iowait_boost
// =============================================================================
class IoWaitBoostManager {
private:
    static constexpr uint32_t IOWAIT_BOOST_MIN = 128;  // SCHED_CAPACITY_SCALE / 8
    static constexpr uint32_t IOWAIT_BOOST_MAX = 1024;
    static constexpr uint64_t IOWAIT_BOOST_DECAY_NS = 50000000;  // 50ms 衰减
    
    uint32_t boost_value_{0};
    uint64_t last_update_ns_{0};
    bool pending_{false};
    
public:
    // 更新 IO-Wait 状态
    void update(bool has_iowait, uint64_t now_ns) noexcept;
    
    // 获取 boost 值 (0-1024)
    uint32_t get_boost() const noexcept { return boost_value_; }
    
    // 是否待 boost
    bool is_pending() const noexcept { return pending_; }
    
    // 重置
    void reset() noexcept;
};

// =============================================================================
// 增强的 Predictor - 双模型协同决策
// =============================================================================
class Predictor {
public:
    enum class Model { LINEAR, NEURAL, HYBRID };
    
private:
    // 线性回归参数 (快速响应)
    std::array<float, 3> linear_weights_{0.3f, -0.1f, 55.0f};
    float ema_error_{2.5f};
    
    // 神经网络
    NeuralPredictor neural_;
    
    // 多时间尺度特征
    MultiScaleFeatures multi_scale_;
    
    // 场景识别器
    SceneClassifier scene_classifier_;
    
    // IO-Wait Boost 管理器
    IoWaitBoostManager io_boost_;
    
    // 异步训练器
    std::unique_ptr<parallel::AsyncTrainer> async_trainer_;
    
    // 当前激活的模型
    Model active_model_{Model::HYBRID};
    
    // 特征历史 (用于计算多时间尺度)
    struct FeatureHistory {
        uint32_t cpu_util_buf_[8]{0};
        uint32_t fps_buf_[8]{0};
        uint32_t frame_interval_buf_[8]{0};
        uint8_t idx_{0};
    } history_;
    
    // 更新多时间尺度特征
    void update_multi_scale_features(const LoadFeature& f, uint64_t now_ns) noexcept;
    
public:
    Predictor() noexcept;
    
    // 主预测接口
    float predict(const LoadFeature& features) noexcept;
    
    // 各模型独立预测
    float predict_linear(const LoadFeature& features) noexcept;
    float predict_neural(const LoadFeature& features) noexcept;
    
    // 场景感知的预测 (多时间尺度 + 场景专项)
    float predict_scene_aware(const LoadFeature& features) noexcept;
    
    // 在线训练
    void train(const LoadFeature& features, float actual_fps) noexcept;
    
    // 异步训练 - 不阻塞主循环
    std::future<void> train_async(const LoadFeature& features, float actual_fps) noexcept;
    
    // 是否正在训练
    bool is_training() const noexcept { 
        return async_trainer_ && async_trainer_->is_training(); 
    }
    
    // 获取当前场景
    SchedScene get_current_scene() const noexcept { 
        return multi_scale_.current_scene; 
    }
    
    // 获取 IO boost 值
    uint32_t get_io_boost() const noexcept { return io_boost_.get_boost(); }

    // IO boost 管理器访问器
    IoWaitBoostManager& io_wait_manager() noexcept { return io_boost_; }
    const IoWaitBoostManager& io_wait_manager() const noexcept { return io_boost_; }

    // 更新多时间尺度特征 (公共接口)
    void update_multiscale_features(const LoadFeature& f, uint64_t now_ns) noexcept {
        update_multi_scale_features(f, now_ns);
    }

    // 模型切换
    void set_model(Model m) noexcept { active_model_ = m; }
    Model get_model() const noexcept { return active_model_; }
    
    // 神经网络访问器
    const NeuralPredictor& neural() const noexcept { return neural_; }
    NeuralPredictor& neural() noexcept { return neural_; }
    
    // 导出/导入权重
    void export_linear(float& w_util, float& w_rq, float& bias, float& ema_err) const noexcept;
    void export_model(float* weights, float* biases) const noexcept;
    void import_linear(float w_util, float w_rq, float bias, float ema_err) noexcept;
    void import_model(const std::vector<std::vector<std::vector<float>>>& nn_weights,
                      const std::vector<std::vector<float>>& nn_biases) noexcept;
};

} // namespace hp::predict