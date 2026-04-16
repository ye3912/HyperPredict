#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace hp::core {

class FramePacer {
public:
    FramePacer() noexcept;
    
    // 初始化 (扫描可用的帧时间采集路径)
    bool init() noexcept;
    
    // 采集一次帧间隔 (建议在 100ms 周期调用)
    // 返回值：帧生成时间间隔 (微秒)，0 表示采集失败
    uint64_t collect() noexcept;
    
    // 获取平滑后的帧间隔 (EMA 滤波)
    uint64_t get_smooth_interval_us() const noexcept;
    
    // 获取瞬时帧率
    float get_instant_fps() const noexcept;
    
    // 是否高刷场景 (>90fps)
    bool is_high_refresh() const noexcept;
    
    // 是否检测到稳定帧率 (连续 N 帧有效)
    bool is_stable() const noexcept { return valid_frame_count_ >= 5; }
    
    // 重置
    void reset() noexcept;
    
private:
    // 帧时间戳环形缓冲区 (存储最近 N 帧的生成时间)
    static constexpr int BUFFER_SIZE = 12;
    std::array<uint64_t, BUFFER_SIZE> frame_intervals_{};
    int write_idx_{0};
    int valid_frame_count_{0};
    
    // EMA 平滑
    uint64_t ema_interval_us_{16666};  // 默认 60fps
    float ema_alpha_{0.2f};
    
    // 采集路径
    std::string sf_surface_;          // SurfaceFlinger 目标 Surface
    std::string drm_path_;            // DRM vblank 路径
    bool has_fpsgo_{false};
    bool has_sf_latency_{false};
    
    // 上次采集时间 (兜底用)
    uint64_t last_collect_time_us_{0};
    
    // 采集方法
    uint64_t collect_surfaceflinger() noexcept;
    uint64_t collect_drm_vblank() noexcept;
    uint64_t collect_fpsgo() noexcept;
    uint64_t collect_fallback() noexcept;
    
    // 解析辅助
    static uint64_t parse_sf_latency(const std::string& line) noexcept;
    static uint64_t parse_drm_timestamp(const std::string& line) noexcept;
};

} // namespace hp::core