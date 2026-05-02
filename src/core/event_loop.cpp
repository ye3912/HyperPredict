#include "core/event_loop.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sched.h>
#include <algorithm>
#include <chrono>
#include <shared_mutex>

namespace hp {

// =============================================================================
// Rate Limiting 常量 - 类比 CNN 论文的查表化简
// =============================================================================
[[maybe_unused]] static constexpr uint64_t RATE_LIMIT_MIN_US = 1000ULL;  // 1ms 最小调频间隔
static constexpr uint64_t RATE_LIMIT_GAME_US = 500ULL; // 游戏模式 500us
static constexpr uint64_t RATE_LIMIT_DAILY_US = 2000ULL; // 日常模式 2ms

EventLoop::EventLoop()
    : epfd_(-1)
    , timer_fd_(-1)
    , period_ms_(100)
    , loop_count_(0)
    , idle_count_(0)
    , running_(false)
    , web_server_(net::DEFAULT_PORT)
    , last_freq_update_us_(0)
    , is_idle_(false)
    , idle_start_time_(0)
    , last_touch_time_(0)
    , idle_step_(0)
    , last_idle_step_time_(0) {
    // 初始化最后触摸时间为当前时间
    last_touch_time_ = std::chrono::steady_clock::now().time_since_epoch().count() / 1000;
}

// ✅ 新增：实现 stop 方法
void EventLoop::stop() noexcept {
    running_ = false;
}

bool EventLoop::init() noexcept {
    LOGI("=== HyperPredict v4.2 Initializing ===");
    
    if (!hw_.analyze()) {
        LOGE("Hardware analysis failed");
        return false;
    }
    
    if (!topo_.detect()) {
        LOGE("Topology detection failed");
        return false;
    }
    
    if (!freq_mgr_.init()) {
        LOGE("FreqManager init failed");
        return false;
    }

    // 构建 CPU-domain 映射
    build_cpu_domain_map();
    
    // 预打开频率 fd
    init_freq_fds();
    
    // 检测调度后端
    detect_sched_backend();
    
    migrator_.init(hw_.profile());
    binder_.init(hw_.profile());
    binder_.bind_sched();
    calibrator_.calibrate(topo_);
    engine_.init(calibrator_.baseline());
    engine_.set_min_freq(hw_.profile().min_freq_khz);  // ✅ 设置最低频率
    
    // Start Web Server
    web_server_.set_delegate(this);
    if (!web_server_.start()) {
        LOGW("Web server failed to start (port may be in use)");
    } else {
        LOGI("Web server listening on port %u", web_server_.port());
    }
    
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) {
        LOGE("epoll_create1 failed: %s", strerror(errno));
        return false;
    }
    
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timer_fd_ < 0) {
        LOGE("timerfd_create failed: %s", strerror(errno));
        close(epfd_);
        return false;
    }
    
    struct itimerspec its{};
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = period_ms_ * 1000000;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = period_ms_ * 1000000;
    
    if (timerfd_settime(timer_fd_, 0, &its, nullptr) < 0) {
        LOGE("timerfd_settime failed: %s", strerror(errno));
        close(timer_fd_);
        close(epfd_);
        return false;
    }
    
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd_;
    
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, timer_fd_, &ev) < 0) {
        LOGE("epoll_ctl failed: %s", strerror(errno));
        close(timer_fd_);
        close(epfd_);
        return false;
    }
    
    LOGI("Initialization complete | Period=%ums | Cores=%d", 
         period_ms_, topo_.get_total_cpus());
    
    return true;
}

void EventLoop::collect() noexcept {
    LoadFeature f = collector_.collect();
    
    // ========== IO-Wait 检测 (P0: 降低误触发) ==========
    // wakeups>120 && util<250 (原: wakeups>80 && util<300)
    if (f.wakeups_100ms > 120 && f.cpu_util < 250) {
        io_wait_detected_++;
        // 传递给 predictor
        predictor_.io_wait_manager().update(true, 0);
    } else {
        io_wait_detected_ = 0;
        predictor_.io_wait_manager().update(false, 0);
    }
    
    // ========== 任务合并: 帧采样优化 ==========
    // 根据场景动态调整采样间隔
    bool is_game = is_gaming_scene(f);
    uint32_t sample_interval = is_game ? SAMPLE_INTERVAL_GAME : SAMPLE_INTERVAL_IDLE;
    
    // 特征更新采样 (减少更新频率)
    if (loop_count_ % sample_interval == 0) {
        auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        predictor_.update_multiscale_features(f, now_ns);
    }
    
    if (!queue_.try_push(f)) {
        LOGW("Queue full, dropping frame");
    }
    
    // ========== 自适应异步训练触发 ==========
    // 根据场景调整训练频率 (游戏更频繁，日常降低)
    uint32_t train_interval = is_game ? SAMPLE_INTERVAL_TRAIN / 2 : SAMPLE_INTERVAL_TRAIN;
    if (loop_count_ - last_training_frame_ >= train_interval && !predictor_.is_training()) {
        float actual_fps = f.frame_interval_us > 0 ? 
            1000000.0f / static_cast<float>(f.frame_interval_us) : 60.0f;
        predictor_.train_async(f, actual_fps);
        last_training_frame_ = loop_count_;
    }
}

bool EventLoop::is_gaming_scene(const LoadFeature& f) noexcept {
    // ========== 基于包名的游戏检测 ==========
    if (f.package_name[0] != '\0') {
        // 使用统一的游戏检测函数
        if (is_game_package(f.package_name)) {
            return true;
        }
    }
    
    // 兜底: 如果包名未知，使用启发式判断
    float fps = 1000000.0f / static_cast<float>(f.frame_interval_us);
    if (fps > 90.0f && f.touch_rate_100ms > 30) {
        return true;
    }
    
    if (f.cpu_util > 800 && f.thermal_margin < 10) {
        return true;
    }
    
    return false;
}

int32_t EventLoop::calculate_fas_delta(const LoadFeature& f, float current_fps, 
                                        float target_fps) noexcept {
    // ===== fas-rs 风格优化 =====
    
    static int32_t last_delta = 0;
    static int32_t frame_error_ema = 0;
    static uint8_t stable_frames = 0;
    
    // 获取硬件配置
    const auto& prof = hw_.profile();
    float sensitivity = prof.fas_sensitivity;
    if (sensitivity <= 0) sensitivity = 1.0f;
    
    // ===== fas-rs: margin_fps = target_fps / 60 * base =====
    // balance 模式: margin_fps = 1, performance: 0, powersave: 3
    uint8_t target_fps_idx = (target_fps >= 144) ? 3 : (target_fps >= 120) ? 2 : (target_fps >= 90) ? 1 : 0;
    static constexpr uint8_t MARGIN_FPS[] = {1, 1, 2, 3};  // [30,60,90,120+]
    uint8_t margin_fps = (target_fps_idx <= 3) ? MARGIN_FPS[target_fps_idx] : 1;
    
    // 1. 计算帧时间 (微秒)
    uint32_t frame_time_us = f.frame_interval_us > 0 ? f.frame_interval_us : 16667;
    uint32_t target_frame_time_us = 1000000 / static_cast<uint32_t>(target_fps);
    
    // fas-rs 核心: 如果帧时间 < target - margin → 降频
    // 如果帧时间 > target + margin → 升频
    int32_t frame_margin_us = static_cast<int32_t>(margin_fps * 1000);  // margin_fps 转微秒
    
    int32_t delta = 0;
    if (frame_time_us < target_frame_time_us - frame_margin_us) {
        // 帧时间充足，可降频
        int32_t spare = static_cast<int32_t>(target_frame_time_us - frame_time_us);
        // spare 越大，降频越多 (负值)
        delta = -(spare * 8 / 10) * static_cast<int32_t>(sensitivity);
    } else if (frame_time_us > target_frame_time_us + frame_margin_us) {
        // 帧时间不足，需要升频
        int32_t lag = static_cast<int32_t>(frame_time_us - target_frame_time_us);
        // lag 越大，升频越多
        delta = (lag * 12 / 10) * static_cast<int32_t>(sensitivity);
    }
    
    // 2. 额外保护: 掉帧时大幅升频
    float fps_error = target_fps - current_fps;
    if (fps_error < -margin_fps) {
        delta += static_cast<int32_t>(-fps_error * 12000);
    }
    
    // 3. 稳帧检测 (fas-rs keep_std)
    bool is_stable = std::abs(fps_error) < margin_fps;
    if (is_stable && stable_frames < 10) {
        stable_frames++;
    } else if (!is_stable) {
        stable_frames = 0;
    }
    
    // 稳帧超 8 帧后强力阻尼 (P0: 原>10 && 0.6f)
    if (stable_frames > 8 && delta > 0) {
        delta = static_cast<int32_t>(delta * 0.4f);  // 原: 0.6f
    }
    delta = static_cast<int32_t>(last_delta * 0.8f + delta * 0.2f);  // 原: 0.7f/0.3f
    
    // 5. 限制范围 (P0: 限制单次跳变)
    int32_t max_delta = static_cast<int32_t>(180000 * sensitivity);  // 原: 250000
    delta = std::clamp(delta, -max_delta, max_delta);
    
    // 6. 死区 (P0: 扩大过滤区)
    if (std::abs(delta) < 35000) {  // 原: 20000
        delta = 0;
    }
    
    last_delta = delta;
    return delta;
}

void EventLoop::apply_freq_config(const FreqConfig& cfg,
                                   const device::FreqDomain& domain) noexcept {
    // 查找 domain 在 freq_mgr_ 中的索引
    int domain_idx = -1;
    const auto& domains = freq_mgr_.domains();
    for (size_t i = 0; i < domains.size(); ++i) {
        if (domains[i].cpus == domain.cpus) {
            domain_idx = static_cast<int>(i);
            break;
        }
    }
    
    // 映射到实际支持的频点 (O(1) LUT)
    uint32_t snapped_target = freq_mgr_.fast_snap(cfg.target_freq, domain_idx);
    uint32_t snapped_min = freq_mgr_.fast_snap(cfg.min_freq, domain_idx);
    
    LOGI("[Snap] idx=%d target=%u->%u min=%u->%u steps=%zu",
         domain_idx, cfg.target_freq, snapped_target, cfg.min_freq, snapped_min,
         domains[domain_idx].steps.size());
    
    // 只写 domain 的代表 CPU — 同频域内所有 CPU 共享时钟，写一个即可
    int rep_cpu = domain.cpus.empty() ? -1 : domain.cpus[0];
    if (rep_cpu < 0 || rep_cpu >= 8) return;
    auto& fc = freq_fds_[rep_cpu];
    
    // 计算补偿频率 (uclamp 不可用时)
    uint32_t effective_min_freq = snapped_min;
    uint32_t effective_max_freq = snapped_target;
    
    if (sched_backend_ != SchedBackend::UCLAMP) {
        // 无 uclamp 支持时，使用频率补偿
        if (sched_backend_ == SchedBackend::CGROUP_V2) {
            // 尝试写入 cgroup v2 cpu.weight (对代表 CPU 写一次即可)
            char cgroup_path[256];
            snprintf(cgroup_path, sizeof(cgroup_path),
                "/sys/fs/cgroup/system/cpu%d/cpu.weight", rep_cpu);
            if (access(cgroup_path, F_OK) == 0) {
                FILE* f = fopen(cgroup_path, "w");
                if (f) {
                    fprintf(f, "%u\n", 100 + cfg.uclamp_min * 100);
                    fclose(f);
                }
            }
        }
            
            // 频率补偿：当 uclamp 限制低优先级时，提高频率上限
            if (cfg.uclamp_min < 50) {
                // 低 uclamp -> 提高频率上限来补偿
                uint32_t boost = domain.max_freq * (50 - cfg.uclamp_min) / 200;
                effective_max_freq = std::min(domain.max_freq, cfg.target_freq + boost);
            }
            
            // 静态调试日志（每 50 次输出一次）
            static int log_count = 0;
            if (++log_count % 50 == 1) {
                LOGD("Freq fallback: Backend=%d | UCLAMP=%u/%u | Freq=%u/%u",
                    static_cast<int>(sched_backend_),
                    cfg.uclamp_min, cfg.uclamp_max,
                    effective_min_freq, effective_max_freq);
            }
        }
        
        // 值缓存 - 避免重复写入相同值
        if (fc.last_min_freq != effective_min_freq) {
            if (fc.min_freq_fd < 0) {
                char path[128];
                snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", rep_cpu);
                fc.min_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
            }
            if (fc.min_freq_fd >= 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%u\n", effective_min_freq);
                ::write(fc.min_freq_fd, buf, len);
                fc.last_min_freq = effective_min_freq;
            }
        }
        
        if (fc.last_max_freq != effective_max_freq) {
            if (fc.max_freq_fd < 0) {
                char path[128];
                snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", rep_cpu);
                fc.max_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
            }
            if (fc.max_freq_fd >= 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%u\n", effective_max_freq);
                ::write(fc.max_freq_fd, buf, len);
                fc.last_max_freq = effective_max_freq;
                LOGI("[Freq] CPU%d max_freq=%u (target=%u)", rep_cpu, effective_max_freq, cfg.target_freq);
            }
        }
        
        // 强制设置当前频率
        if (fc.cur_freq_fd < 0) {
            char path[128];
            snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", rep_cpu);
            fc.cur_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
        }
        if (fc.cur_freq_fd >= 0 && fc.last_cur_freq != effective_max_freq) {
            char buf[16];
            int len = snprintf(buf, sizeof(buf), "%u\n", effective_max_freq);
            ::write(fc.cur_freq_fd, buf, len);
            fc.last_cur_freq = effective_max_freq;
            LOGI("[Freq] CPU%d cur_freq=%u", rep_cpu, effective_max_freq);
        }
        
        // UCLAMP 写入 (仅当支持时)
        if (sched_backend_ == SchedBackend::UCLAMP) {
            if (fc.last_uclamp_min != cfg.uclamp_min) {
                if (fc.uclamp_min_fd < 0) {
                    char path[128];
                    snprintf(path, sizeof(path), "/dev/cpuctl/cpu%d/uclamp.min", rep_cpu);
                    fc.uclamp_min_fd = ::open(path, O_WRONLY | O_CLOEXEC);
                }
                if (fc.uclamp_min_fd >= 0) {
                    char buf[8];
                    int len = snprintf(buf, sizeof(buf), "%u\n", cfg.uclamp_min);
                    ::write(fc.uclamp_min_fd, buf, len);
                    fc.last_uclamp_min = cfg.uclamp_min;
                }
            }
            
            if (fc.last_uclamp_max != cfg.uclamp_max) {
                if (fc.uclamp_max_fd < 0) {
                    char path[128];
                    snprintf(path, sizeof(path), "/dev/cpuctl/cpu%d/uclamp.max", rep_cpu);
                    fc.uclamp_max_fd = ::open(path, O_WRONLY | O_CLOEXEC);
                }
                if (fc.uclamp_max_fd >= 0) {
                    char buf[8];
                    int len = snprintf(buf, sizeof(buf), "%u\n", cfg.uclamp_max);
                    ::write(fc.uclamp_max_fd, buf, len);
                    fc.last_uclamp_max = cfg.uclamp_max;
                }
            }
        }
}

void EventLoop::process() noexcept {
    auto f_opt = queue_.try_pop();
    if (!f_opt) return;

    const LoadFeature& f = *f_opt;

    // Store latest feature for web queries (读写分离优化)
    {
        std::unique_lock<std::shared_mutex> lock(latest_mutex_);
        latest_feature_ = f;
    }

    // ========== 新增: 空闲状态检测 ==========
    check_idle_state(f);

    // ========== 空闲状态下直接应用最低频率 ==========
    if (is_idle_) {
        apply_idle_freq();
        return;  // 跳过正常的调频逻辑
    }

    bool is_game = is_gaming_scene(f);

    float actual_fps = f.frame_interval_us > 0 ?
                      1000000.0f / static_cast<float>(f.frame_interval_us) : 60.0f;

    int cur_cpu = sched_getcpu();

    // ========== 新增: Rate Limiting ==========
    auto now_us = std::chrono::steady_clock::now().time_since_epoch().count() / 1000;

    // 根据场景设置限速间隔
    rate_limit_us_ = is_game ? RATE_LIMIT_GAME_US : RATE_LIMIT_DAILY_US;

    // ========== SchedHorizon 风格频率策略 ==========
    // 跳过：SchedHorizon 实时计算，不需要限速
    // 目标频率 = 负载 + margin (每帧实时计算)
    
    // Update migration engine with current load (including wakeups for task classification)
    migrator_.update(cur_cpu, f.cpu_util, f.run_queue_len, f.wakeups_100ms);

    // 使用 CPU-domain 映射快速查找 domain (O(1))
    int domain_idx = 0;
    if (cur_cpu >= 0 && cur_cpu < 8 && cpu_to_domain_map_[cur_cpu] >= 0) {
        domain_idx = cpu_to_domain_map_[cur_cpu];
    } else {
        // 回退到线性查找
        const auto& domains = freq_mgr_.domains();
        for (size_t i = 0; i < domains.size(); ++i) {
            for (int cpu : domains[i].cpus) {
                if (cpu == cur_cpu) {
                    domain_idx = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    const auto& domain = freq_mgr_.domains()[domain_idx];
    
    // ========== 新增: 增强的场景识别 ==========
    // 使用增强的 Predictor 进行场景识别
    predict::SchedScene current_scene = predictor_.get_current_scene();
    
    // 获取日常调频参数 (论文参考)
    const auto& daily_cfg = hw_.profile().daily;
    
    FreqConfig cfg;
    device::MigResult mig_result{};  // 在外面声明
    
    // ========== Step 1: 先迁移 (水平放置) ==========
    float target_fps = is_game ? 120.0f : 60.0f;
    migrator_.set_edp_target_fps(target_fps);
    
    if (loop_count_ % 5 == 0) {
        mig_result = migrator_.decide(cur_cpu, static_cast<uint32_t>(f.thermal_margin), is_game);
    }
    
    // 核心选择信息传给 PolicyEngine（协同）
    FreqConfig migration_hint{};
    if (mig_result.go && mig_result.target >= 0) {
        migration_hint.prefer_big = (mig_result.target >= 4);  // 大核索引>=4
        migration_hint.prefer_little = (mig_result.target < 4);   // 小核索引<4
    }
    
    // ========== 游戏模式: FAS 主导频率 ==========
    if (is_game) {
        // 基础频率 = 中间频率
        cfg.target_freq = (domain.min_freq + domain.max_freq) / 2;
        cfg.min_freq = domain.min_freq;
        
        // FAS 调频
        int32_t fas_delta = calculate_fas_delta(f, actual_fps, target_fps);
        cfg.target_freq = static_cast<uint32_t>(
            std::clamp(static_cast<int32_t>(cfg.target_freq) + fas_delta,
                    static_cast<int32_t>(domain.min_freq),
                    static_cast<int32_t>(domain.max_freq))
        );
        
        // 传入当前频率供 MigrationEngine EDP 计算（Step 3）
        migrator_.set_edp_target_fps(target_fps);
        migrator_.set_current_freq(cfg.target_freq);  // 频率感知
        
        // 温度调整
        if (f.thermal_margin < 10) {
            cfg.target_freq = static_cast<uint32_t>(cfg.target_freq * 0.85f);
        }
        
        cfg.uclamp_min = 50;
        cfg.uclamp_max = 100;
    } else {
        // ========== 非游戏模式: PolicyEngine + MigrationEngine 协同 ==========
        // 使用 PolicyEngine 的 E-Mapper 风格调度
        cfg = engine_.decide(f, target_fps, current_scene);
        
        // 边界约束
        cfg.target_freq = std::clamp(cfg.target_freq, domain.min_freq, domain.max_freq);
        cfg.min_freq = std::clamp(cfg.min_freq, domain.min_freq, cfg.target_freq);
    }
    
    apply_freq_config(cfg, domain);
    
    // 执行迁移（使用前面计算的结果）
    bool should_migrate = (loop_count_ % 5 == 0 && mig_result.go && mig_result.target != cur_cpu);
    if (should_migrate) {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(mig_result.target, &mask);
        
        // 日常场景: 允许更多核心灵活性
        if (!is_game) {
            // 允许调度到相邻核心
            if (mig_result.target > 0) CPU_SET(mig_result.target - 1, &mask);
            if (mig_result.target < static_cast<int>(topo_.get_total_cpus()) - 1) {
                CPU_SET(mig_result.target + 1, &mask);
            }
        }
        
        // 温控紧急情况
        if (mig_result.thermal) {
            CPU_ZERO(&mask);
            CPU_SET(mig_result.target, &mask);
        }
        
        if (sched_setaffinity(0, sizeof(mask), &mask) == 0) {
            LOGD("Migrate: CPU%d→%d | Util=%u | Therm=%d",
                 cur_cpu, mig_result.target, f.cpu_util, f.thermal_margin);
        }
    }
    
    if (loop_count_ % 20 == 0) {
        // ========== 增强的日志输出 ==========
        const char* scene_names[] = {"IDLE", "LIGHT", "MEDIUM", "VIDEO", "HEAVY", "BOOST", "IOWAIT"};
        LOGI("[HyperPredict] Freq=%u | FPS=%.1f | Scene=%s | IOBoost=%u | RateLimit=%u us",
             cfg.target_freq, actual_fps,
             scene_names[static_cast<int>(current_scene)],
             predictor_.get_io_boost(),
             rate_limit_us_);
    }
}

void EventLoop::adjust(bool increase) noexcept {
    if (increase) {
        period_ms_ = std::max(20u, period_ms_ - 10);
    } else {
        idle_count_++;
        if (idle_count_ > 100) {
            period_ms_ = std::min(200u, period_ms_ + 5);
        }
    }
    
    if (timer_fd_ >= 0) {
        struct itimerspec its{};
        its.it_value.tv_nsec = period_ms_ * 1000000;
        its.it_interval.tv_nsec = period_ms_ * 1000000;
        timerfd_settime(timer_fd_, 0, &its, nullptr);
    }
}

void EventLoop::cleanup() noexcept {
    web_server_.stop();
    
    if (timer_fd_ >= 0) {
        close(timer_fd_);
        timer_fd_ = -1;
    }
    if (epfd_ >= 0) {
        close(epfd_);
        epfd_ = -1;
    }
    // 关闭缓存的 sysfs fd
    for (auto& fc : freq_fds_) {
        if (fc.min_freq_fd >= 0) close(fc.min_freq_fd);
        if (fc.max_freq_fd >= 0) close(fc.max_freq_fd);
        if (fc.uclamp_min_fd >= 0) close(fc.uclamp_min_fd);
        if (fc.uclamp_max_fd >= 0) close(fc.uclamp_max_fd);
    }
}

// WebServerDelegate implementations

net::StatusUpdate EventLoop::get_status() {
    net::StatusUpdate status;
    
    LoadFeature f;
    {
        std::shared_lock<std::shared_mutex> lock(latest_mutex_);
        f = latest_feature_;
    }
    
    status.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    status.fps = f.frame_interval_us > 0 ? 1000000 / f.frame_interval_us : 60;
    status.target_fps = (current_mode_ == 2) ? 120 : (current_mode_ == 1) ? 90 : 60;
    status.cpu_util = f.cpu_util;
    status.run_queue_len = f.run_queue_len;
    status.wakeups_100ms = f.wakeups_100ms;
    status.frame_interval_us = f.frame_interval_us;
    status.touch_rate_100ms = f.touch_rate_100ms;
    status.thermal_margin = f.thermal_margin;
    status.temperature = 42;  // Placeholder
    status.battery_level = f.battery_level;
    status.is_gaming = f.is_gaming;
    status.mode = (current_mode_ == 2) ? "turbo" : (current_mode_ == 1) ? "game" : "daily";
    status.uclamp_min = uclamp_min_.load();
    status.uclamp_max = uclamp_max_.load();
    
    return status;
}

net::ModelWeights EventLoop::get_model_weights() {
    net::ModelWeights weights;
    
    // 获取线性回归权重
    predictor_.export_linear(weights.w_util, weights.w_rq, weights.bias, weights.ema_error);
    
    // 简化: 使用固定权重 (实际可从特征提取器获取)
    weights.w_wakeups = 0.05f;
    weights.w_frame = 0.2f;
    weights.w_touch = 0.02f;
    weights.w_thermal = 0.1f;
    weights.w_battery = 0.01f;
    
    // 获取神经网络状态
    weights.has_nn = (predictor_.get_model() == predict::Predictor::Model::NEURAL);
    
    // 导出神经网络权重
    float nn_weights[32 + 4];  // wh(32) + wo(4)
    float nn_biases[4 + 1];     // bh(4) + bo(1)
    predictor_.export_model(nn_weights, nn_biases);
    
    weights.nn_weights = std::vector<std::vector<std::vector<float>>>(2);
    // 层1: 4×8
    weights.nn_weights[0].resize(4);
    for (size_t h = 0; h < 4; h++) {
        weights.nn_weights[0][h].resize(8);
        for (size_t i = 0; i < 8; i++) {
            weights.nn_weights[0][h][i] = nn_weights[h * 8 + i];
        }
    }
    // 层2: 1×4
    weights.nn_weights[1].resize(1);
    weights.nn_weights[1][0].resize(4);
    for (size_t h = 0; h < 4; h++) {
        weights.nn_weights[1][0][h] = nn_weights[32 + h];
    }
    
    weights.nn_biases.resize(2);
    weights.nn_biases[0] = std::vector<float>(nn_biases, nn_biases + 4);
    weights.nn_biases[1] = std::vector<float>(1, nn_biases[4]);
    
    return weights;
}

bool EventLoop::set_model_weights(const net::ModelWeights& w) {
    // 设置线性回归权重
    predictor_.import_linear(w.w_util, w.w_rq, w.bias, w.ema_error);
    
    // 设置神经网络权重
    if (w.nn_weights.size() >= 2 && w.nn_biases.size() >= 2) {
        predictor_.import_model(w.nn_weights, w.nn_biases);
    }
    
    // 设置激活的模型
    if (w.has_nn) {
        predictor_.set_model(predict::Predictor::Model::NEURAL);
    } else {
        predictor_.set_model(predict::Predictor::Model::LINEAR);
    }
    
    LOGI("Model weights updated: %s", w.has_nn ? "NEURAL" : "LINEAR");
    return true;
}

bool EventLoop::handle_command(const net::WebCommand& cmd) {
    LOGI("Web command: %s", cmd.cmd.c_str());
    
    if (cmd.cmd == "set_mode") {
        std::string mode;
        if (cmd.get_string("mode", mode)) {
            if (mode == "daily") {
                current_mode_ = 0;
                LOGI("Mode set to daily");
            } else if (mode == "game") {
                current_mode_ = 1;
                LOGI("Mode set to game");
            } else if (mode == "turbo") {
                current_mode_ = 2;
                LOGI("Mode set to turbo");
            }
            return true;
        }
    } else if (cmd.cmd == "set_uclamp") {
        int min_val, max_val;
        if (cmd.get_int("min", min_val)) {
            uclamp_min_ = static_cast<uint8_t>(std::clamp(min_val, 0, 100));
        }
        if (cmd.get_int("max", max_val)) {
            uclamp_max_ = static_cast<uint8_t>(std::clamp(max_val, 0, 100));
        }
        LOGI("uclamp set: %u-%u", uclamp_min_.load(), uclamp_max_.load());
        return true;
    } else if (cmd.cmd == "set_thermal") {
        std::string preset;
        if (cmd.get_string("preset", preset)) {
            thermal_preset_ = preset;
            LOGI("Thermal preset set: %s", preset.c_str());
            return true;
        }
    } else if (cmd.cmd == "set_model") {
        std::string model;
        if (cmd.get_string("model", model)) {
            if (model == "linear") {
                predictor_.set_model(predict::Predictor::Model::LINEAR);
                LOGI("Predictor model: LINEAR");
            } else if (model == "neural") {
                predictor_.set_model(predict::Predictor::Model::NEURAL);
                LOGI("Predictor model: NEURAL");
            }
            return true;
        }
    }
    
    return false;
}

void EventLoop::save() noexcept {
    LOGI("Saving state...");
}

void EventLoop::start() noexcept {
    if (!init()) {
        LOGE("Initialization failed, exiting");
        return;
    }
    
    running_ = true;
        uint32_t fc = 0;
    const uint32_t ci = (period_ms_ > 100) ? 20 : 5;
    
    LOGI("=== Event Loop Started ===");
    
    while (running_.load(std::memory_order_relaxed)) {
        loop_count_++;
        
        struct epoll_event ev[4];
        int n = epoll_wait(epfd_, ev, 4, period_ms_);
        
        if (n < 0) {
            if (errno == EINTR) {
                usleep(period_ms_ * 1000);
                continue;
            }
            LOGE("epoll_wait error: %s", strerror(errno));
            break;
        }
        
        if (n == 0) {
            idle_count_++;
            if (idle_count_ > 50) {
                adjust(false);
            }
            continue;
        }
        
        idle_count_ = 0;
        
        for (int i = 0; i < n; ++i) {
            if (ev[i].data.fd == timer_fd_) {
                uint64_t buf;
                ssize_t bytes = read(timer_fd_, &buf, 8);
                if (bytes == 8) {
                    process();
                }
            }
        }
        
        if (++fc >= ci) {
            collect();
            fc = 0;
        }
    }
    
    LOGI("=== Event Loop Stopped ===");
    save();
    cleanup();
}

bool EventLoop::detect_sched_backend() noexcept {
    // 1. 检测 uclamp (cgroup v1)
    if (access("/dev/cpuctl/cpu0/uclamp.min", F_OK) == 0) {
        sched_backend_ = SchedBackend::UCLAMP;
        LOGI("Sched Backend: UCLAMP (cgroup v1)");
        return true;
    }
    
    // 2. 检测 cgroup v2
    if (access("/sys/fs/cgroup/cgroup.subtree_control", F_OK) == 0) {
        sched_backend_ = SchedBackend::CGROUP_V2;
        LOGI("Sched Backend: CGROUP_V2");
        return true;
    }
    
    // 3. 检测 cgroup v1 cpu
    if (access("/sys/fs/cgroup/cpu", F_OK) == 0) {
        sched_backend_ = SchedBackend::CGROUP_V2;
        LOGI("Sched Backend: CGROUP_V1 (legacy)");
        return true;
    }
    
    // 4. 仅频率控制 (无 cgroup 支持)
    sched_backend_ = SchedBackend::FREQ_ONLY;
    LOGW("Sched Backend: FREQ_ONLY (no cgroup support)");
    LOGW("UCLAMP not available - using frequency compensation");
    
    return false;
}

uint32_t EventLoop::get_compensated_freq(uint32_t base_freq, uint8_t uclamp_min) const noexcept {
    // 当 uclamp 不可用时，通过提高 CPU 频率来补偿
    // uclamp_min 范围 0-100，转换为补偿系数

    if (uclamp_min == 0) {
        // 最低优先级，使用最低频率
        return freq_mgr_.domains().empty() ? base_freq : freq_mgr_.domains()[0].min_freq;
    }

    if (uclamp_min >= 100) {
        // 最高优先级，全速运行
        return freq_mgr_.domains().empty() ? base_freq : freq_mgr_.domains()[0].max_freq;
    }

    // 线性插值: uclamp_min 0% -> 最低频率, uclamp_min 100% -> 最高频率
    if (freq_mgr_.domains().empty()) {
        return base_freq;
    }

    const auto& domain = freq_mgr_.domains()[0];
    uint32_t freq_range = domain.max_freq - domain.min_freq;
    uint32_t compensated = domain.min_freq + (freq_range * uclamp_min / 100);

    // 加上基准频率的 50% 作为基础
    compensated = std::max(compensated, base_freq * 50 / 100);

    return std::clamp(compensated, domain.min_freq, domain.max_freq);
}

// =============================================================================
// 空闲状态检测 - 独立于模型控制之外
// =============================================================================

void EventLoop::check_idle_state(const LoadFeature& f) noexcept {
    auto now_us = std::chrono::steady_clock::now().time_since_epoch().count() / 1000;

    // 检测触摸事件
    if (f.touch_rate_100ms > 0) {
        last_touch_time_ = now_us;
    }

    // 检测整机负载小于5%
    bool low_load = (f.cpu_util < IDLE_LOAD_THRESHOLD);

    // 检测无触摸2分钟
    bool no_touch = (now_us - last_touch_time_) > IDLE_TOUCH_TIMEOUT_US;

    // 检测是否处于空闲状态
    bool should_idle = low_load && no_touch;

    if (should_idle && !is_idle_) {
        // 进入空闲状态
        is_idle_ = true;
        idle_start_time_ = now_us;
        idle_step_ = 0;  // 重置下探档位
        last_idle_step_time_ = now_us;
        LOGI("Entering idle state: load=%u, no_touch=%lu us",
             f.cpu_util, now_us - last_touch_time_);
    } else if (!should_idle && is_idle_) {
        // 违反空闲状态条件，立即退出下探
        is_idle_ = false;
        idle_step_ = 0;  // 重置下探档位
        LOGI("Exiting idle state: load=%u, touch_rate=%u",
             f.cpu_util, f.touch_rate_100ms);
    }
}

void EventLoop::apply_idle_freq() noexcept {
    if (!is_idle_) {
        return;
    }

    auto now_us = std::chrono::steady_clock::now().time_since_epoch().count() / 1000;

    // 检查是否到了下探时间
    if (now_us - last_idle_step_time_ > IDLE_STEP_INTERVAL_US) {
        // 增加下探档位
        if (idle_step_ < IDLE_MAX_STEPS) {
            idle_step_++;
            last_idle_step_time_ = now_us;
            LOGI("Idle step %zu/%zu", idle_step_, IDLE_MAX_STEPS);
        }
    }

    // 计算当前档位的频率
    const auto& domains = freq_mgr_.domains();
    if (domains.empty()) {
        return;
    }

    // 获取最高频率和最低频率
    uint32_t max_freq = domains[0].max_freq;
    uint32_t min_freq = hw_.profile().min_freq_khz;

    // 计算当前档位的频率 (从当前频率开始，每次降 20%)
    // 档位 0: 80% 当前频率
    // 档位 1: 64% 当前频率
    // 档位 2: 51% 当前频率
    // 档位 3: 41% 当前频率
    // 档位 4: 33% 当前频率
    // 档位 5: min_freq
    uint32_t target_freq;
    if (idle_step_ >= IDLE_MAX_STEPS) {
        target_freq = min_freq;
    } else {
        // 从 max_freq 开始，每次降 20%
        uint32_t current_freq = max_freq;
        // 每次降 20%: 0.8^0=1.0, 0.8^1=0.8, 0.8^2=0.64...
        float ratio = std::pow(0.7f, static_cast<float>(idle_step_));  // P2: 原 0.8f
        target_freq = std::max(static_cast<uint32_t>(current_freq * ratio), min_freq);
    }

    // 应用频率到所有核心 (每个 domain 只写代表 CPU，同频域共享时钟)
    for (const auto& domain : domains) {
        int rep_cpu = domain.cpus.empty() ? -1 : domain.cpus[0];
        if (rep_cpu < 0 || rep_cpu >= 8) continue;

        // 使用缓存的文件描述符
        auto& fc = freq_fds_[rep_cpu];

        // 设置最小频率
        if (fc.last_min_freq != target_freq) {
            if (fc.min_freq_fd < 0) {
                char path[128];
                snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", rep_cpu);
                fc.min_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
            }
            if (fc.min_freq_fd >= 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%u\n", target_freq);
                ::write(fc.min_freq_fd, buf, len);
                fc.last_min_freq = target_freq;
            }
        }

        // 设置最大频率
        if (fc.last_max_freq != target_freq) {
            if (fc.max_freq_fd < 0) {
                char path[128];
                snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", rep_cpu);
                fc.max_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
            }
            if (fc.max_freq_fd >= 0) {
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%u\n", target_freq);
                ::write(fc.max_freq_fd, buf, len);
                fc.last_max_freq = target_freq;
            }
        }

        // 设置 uclamp.min = 0
        if (sched_backend_ == SchedBackend::UCLAMP) {
            if (fc.last_uclamp_min != 0) {
                if (fc.uclamp_min_fd < 0) {
                    char path[128];
                    snprintf(path, sizeof(path), "/dev/cpuctl/cpu%d/uclamp.min", rep_cpu);
                    fc.uclamp_min_fd = ::open(path, O_WRONLY | O_CLOEXEC);
                }
                if (fc.uclamp_min_fd >= 0) {
                    char buf[8];
                    int len = snprintf(buf, sizeof(buf), "0\n");
                    ::write(fc.uclamp_min_fd, buf, len);
                    fc.last_uclamp_min = 0;
                }
            }
        }
    }

    // 每 60 秒输出一次日志
    static uint64_t last_log_time = 0;
    if (now_us - last_log_time > 60000000ULL) {
        LOGI("Idle state active: step=%zu/%zu, freq=%u kHz",
             idle_step_, IDLE_MAX_STEPS, target_freq);
        last_log_time = now_us;
    }
}

// =============================================================================
// CPU-domain 映射优化 - O(n) → O(1)
// =============================================================================

bool EventLoop::init_freq_fds() noexcept {
    const auto& domains = freq_mgr_.domains();
    
    // 预打开每个 freq domain 代表核心的 fd
    for (size_t d = 0; d < domains.size(); ++d) {
        const auto& domain = domains[d];
        if (domain.cpus.empty()) continue;
        
        // 取每个 domain 的第一个 CPU 作为代表
        int representative_cpu = domain.cpus[0];
        if (representative_cpu < 0 || representative_cpu >= 8) continue;
        
        auto& fc = freq_fds_[representative_cpu];
        
        // 打开 scaling_min_freq
        char path[128];
        snprintf(path, sizeof(path),
            "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", representative_cpu);
        fc.min_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
        
        // 打开 scaling_max_freq
        snprintf(path, sizeof(path),
            "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", representative_cpu);
        fc.max_freq_fd = ::open(path, O_WRONLY | O_CLOEXEC);
        
        // 打开 uclamp_min (如果支持)
        snprintf(path, sizeof(path),
            "/proc/self/uid/%d/cpu.uclamp.min", representative_cpu);
        fc.uclamp_min_fd = ::open(path, O_WRONLY | O_CLOEXEC);
        
        // 打开 uclamp_max
        snprintf(path, sizeof(path),
            "/proc/self/uid/%d/cpu.uclamp.max", representative_cpu);
        fc.uclamp_max_fd = ::open(path, O_WRONLY | O_CLOEXEC);
    }
    
    LOGI("Frequency FD pre-initialization complete");
    return true;
}

void EventLoop::build_cpu_domain_map() noexcept {
    const auto& domains = freq_mgr_.domains();

    // 初始化映射为 -1
    cpu_to_domain_map_.fill(-1);

    // 构建 CPU 到 domain 的映射
    for (size_t i = 0; i < domains.size(); ++i) {
        for (int cpu : domains[i].cpus) {
            if (cpu >= 0 && cpu < 8) {
                cpu_to_domain_map_[cpu] = static_cast<int>(i);
            }
        }
    }

    LOGI("CPU-domain map built:");
    for (int cpu = 0; cpu < 8; ++cpu) {
        if (cpu_to_domain_map_[cpu] >= 0) {
            LOGI("  CPU%d -> Domain %d", cpu, cpu_to_domain_map_[cpu]);
        }
    }
}

} // namespace hp