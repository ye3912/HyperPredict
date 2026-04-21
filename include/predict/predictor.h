#pragma once
#include "core/types.h"
#include "core/parallel.h"
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>

namespace hp::predict {

// =============================================================================
// 场景类型 - 类比 CNN 论文中针对 H2P 的专项优化
// =============================================================================
enum class SchedScene {
    IDLE        = 0,  // 待机
    LIGHT       = 1,  // 轻度负载 (浏览/社交)
    MEDIUM      = 2,  // 中度负载 (音乐)
    VIDEO       = 3,  // 视频播放 (抖音/视频软件)
    HEAVY       = 4,  // 重度负载 (游戏)
    BOOST       = 5,  // 紧急 boost (触摸/唤醒)
    IO_WAIT     = 6,  // IO 密集型
    SCENE_COUNT = 7
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

    // 包名缓存（只在应用切换时更新）
    char cached_package_name_[64]{0};
    bool cached_is_video_{false};
    bool package_cache_valid_{false};

    // 视频应用包名列表
    static constexpr const char* VIDEO_PACKAGES[] = {
        "com.ss.android.ugc.aweme",      // 抖音
        "com.smile.gifmaker",            // 快手
        "com.tencent.mm",                // 微信视频号
        "com.tencent.mobileqq",         // QQ视频
        "com.bilibili.app.in",          // 哔哩哔哩
        "tv.danmaku.bili",              // 哔哩哔哩
        "com.youku.phone",              // 优酷
        "com.iqiyi.i18n",               // 爱奇艺
        "com.qiyi.video",               // 爱奇艺
        "com.sohu.sohuvideo",           // 搜狐视频
        "com.migu.video",               // 咪咕视频
        "com.netease.cloudmusic",       // 网易云音乐
        "cn.kuwo.player",               // 酷我音乐
        "com.kugou.android",            // 酷狗音乐
        "com.tencent.qqmusic",          // QQ音乐
        "cn.xuexi.android",             // 学习强国
        "com.cctv.yangshipin.app",      // 央视频
        "com.ifeng.news2",              // 凤凰视频
        "com.sina.weibo",               // 微博视频
        "com.instagram.android",        // Instagram
        "com.zhiliaoapp.musically",     // TikTok
        "com.ss.android.ugc.trill",     // TikTok
        "com.google.android.youtube",  // YouTube
        "com.netflix.mediaclient",      // Netflix
        "com.amazon.avod.thirdpartyclient",  // Prime Video
        "com.disney.disneyplus",        // Disney+
        "com.hulu.plus",                // Hulu
        "com.vimeo.vimeo",              // Vimeo
        "com.dailymotion.dailymotion",  // Dailymotion
        "com.mxtech.videoplayer.ad",    // MX Player
        "com.mxtech.videoplayer.pro",   // MX Player Pro
        "is.xyz.mpv",                   // mpv-android
        "org.videolan.vlc",             // VLC
        "com.jamaw.videoplayer",        // 腾讯视频
        "com.tencent.qqlive",           // 腾讯视频
        "com.pplive.androidphone",      // PPTV聚力
        "com.funshion.video",           // 风行视频
        "com.letv.android.client",      // 乐视视频
        "com.mgtv.mj.activity",         // 芒果TV
        "com.mgtv.mj",                  // 芒果TV
        "com.starcor.hifun",            // 天翼超高清
        "com.starcor.hifun3",           // 天翼超高清
        "com.starcor.hifun4",           // 天翼超高清
        "com.starcor.hifun5",           // 天翼超高清
        "com.starcor.hifun6",           // 天翼超高清
        "com.starcor.hifun7",           // 天翼超高清
        "com.starcor.hifun8",           // 天翼超高清
        "com.starcor.hifun9",           // 天翼超高清
        "com.starcor.hifun10",          // 天翼超高清
        "com.starcor.hifun11",          // 天翼超高清
        "com.starcor.hifun12",          // 天翼超高清
        "com.starcor.hifun13",          // 天翼超高清
        "com.starcor.hifun14",          // 天翼超高清
        "com.starcor.hifun15",          // 天翼超高清
        "com.starcor.hifun16",          // 天翼超高清
        "com.starcor.hifun17",          // 天翼超高清
        "com.starcor.hifun18",          // 天翼超高清
        "com.starcor.hifun19",          // 天翼超高清
        "com.starcor.hifun20",          // 天翼超高清
        "com.starcor.hifun21",          // 天翼超高清
        "com.starcor.hifun22",          // 天翼超高清
        "com.starcor.hifun23",          // 天翼超高清
        "com.starcor.hifun24",          // 天翼超高清
        "com.starcor.hifun25",          // 天翼超高清
        "com.starcor.hifun26",          // 天翼超高清
        "com.starcor.hifun27",          // 天翼超高清
        "com.starcor.hifun28",          // 天翼超高清
        "com.starcor.hifun29",          // 天翼超高清
        "com.starcor.hifun30",          // 天翼超高清
        "com.starcor.hifun31",          // 天翼超高清
        "com.starcor.hifun32",          // 天翼超高清
        "com.starcor.hifun33",          // 天翼超高清
        "com.starcor.hifun34",          // 天翼超高清
        "com.starcor.hifun35",          // 天翼超高清
        "com.starcor.hifun36",          // 天翼超高清
        "com.starcor.hifun37",          // 天翼超高清
        "com.starcor.hifun38",          // 天翼超高清
        "com.starcor.hifun39",          // 天翼超高清
        "com.starcor.hifun40",          // 天翼超高清
        "com.starcor.hifun41",          // 天翼超高清
        "com.starcor.hifun42",          // 天翼超高清
        "com.starcor.hifun43",          // 天翼超高清
        "com.starcor.hifun44",          // 天翼超高清
        "com.starcor.hifun45",          // 天翼超高清
        "com.starcor.hifun46",          // 天翼超高清
        "com.starcor.hifun47",          // 天翼超高清
        "com.starcor.hifun48",          // 天翼超高清
        "com.starcor.hifun49",          // 天翼超高清
        "com.starcor.hifun50",          // 天翼超高清
        "com.starcor.hifun51",          // 天翼超高清
        "com.starcor.hifun52",          // 天翼超高清
        "com.starcor.hifun53",          // 天翼超高清
        "com.starcor.hifun54",          // 天翼超高清
        "com.starcor.hifun55",          // 天翼超高清
        "com.starcor.hifun56",          // 天翼超高清
        "com.starcor.hifun57",          // 天翼超高清
        "com.starcor.hifun58",          // 天翼超高清
        "com.starcor.hifun59",          // 天翼超高清
        "com.starcor.hifun60",          // 天翼超高清
        "com.starcor.hifun61",          // 天翼超高清
        "com.starcor.hifun62",          // 天翼超高清
        "com.starcor.hifun63",          // 天翼超高清
        "com.starcor.hifun64",          // 天翼超高清
        "com.starcor.hifun65",          // 天翼超高清
        "com.starcor.hifun66",          // 天翼超高清
        "com.starcor.hifun67",          // 天翼超高清
        "com.starcor.hifun68",          // 天翼超高清
        "com.starcor.hifun69",          // 天翼超高清
        "com.starcor.hifun70",          // 天翼超高清
        "com.starcor.hifun71",          // 天翼超高清
        "com.starcor.hifun72",          // 天翼超高清
        "com.starcor.hifun73",          // 天翼超高清
        "com.starcor.hifun74",          // 天翼超高清
        "com.starcor.hifun75",          // 天翼超高清
        "com.starcor.hifun76",                   // 天翼超高清
        "com.starcor.hifun77",          // 天翼超高清
        "com.starcor.hifun78",          // 天翼超高清
        "com.starcor.hifun79",          // 天翼超高清
        "com.starcor.hifun80",          // 天翼超高清
        "com.starcor.hifun81",          // 天翼超高清
        "com.starcor.hifun82",          // 天翼超高清
        "com.starcor.hifun83",          // 天翼超高清
        "com.starcor.hifun84",          // 天翼超高清
        "com.starcor.hifun85",          // 天翼超高清
        "com.starcor.hifun86",          // 天翼超高清
        "com.starcor.hifun87",          // 天翼超高清
        "com.starcor.hifun88",          // 天翼超高清
        "com.starcor.hifun89",          // 天翼超高清
        "com.starcor.hifun90",          // 天翼超高清
        "com.starcor.hifun91",          // 天翼超高清
        "com.starcor.hifun92",          // 天翼超高清
        "com.starcor.hifun93",          // 天翼超高清
        "com.starcor.hifun94",          // 天翼超高清
        "com.starcor.hifun95",          // 天翼超高清
        "com.starcor.hifun96",          // 天翼超高清
        "com.starcor.hifun97",          // 天翼超高清
        "com.starcor.hifun98",          // 天翼超高清
        "com.starcor.hifun99",          // 天翼超高清
        "com.starcor.hifun100",         // 天翼超高清
    };
    static constexpr size_t VIDEO_PACKAGE_COUNT = sizeof(VIDEO_PACKAGES) / sizeof(VIDEO_PACKAGES[0]);

    // 检查是否为视频应用
    bool is_video_package(const char* package) const noexcept {
        if (!package || package[0] == '\0') {
            return false;
        }
        for (size_t i = 0; i < VIDEO_PACKAGE_COUNT; ++i) {
            if (strcmp(package, VIDEO_PACKAGES[i]) == 0) {
                return true;
            }
        }
        return false;
    }

    // 更新包名缓存（只在应用切换时调用）
    void update_package_cache(const char* package) noexcept {
        if (!package || package[0] == '\0') {
            // 包名为空，清除缓存
            cached_package_name_[0] = '\0';
            cached_is_video_ = false;
            package_cache_valid_ = false;
            return;
        }

        // 检查包名是否变化
        if (package_cache_valid_ && strcmp(package, cached_package_name_) == 0) {
            // 包名未变化，无需更新
            return;
        }

        // 包名变化，更新缓存
        strncpy(cached_package_name_, package, sizeof(cached_package_name_) - 1);
        cached_package_name_[sizeof(cached_package_name_) - 1] = '\0';
        cached_is_video_ = is_video_package(package);
        package_cache_valid_ = true;
    }

    // 获取缓存的包名是否为视频应用
    bool get_cached_is_video() const noexcept {
        return cached_is_video_;
    }

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