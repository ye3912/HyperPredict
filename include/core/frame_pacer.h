#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace hp::core {

class FramePacer {
public:
    FramePacer() noexcept;
    
    bool init() noexcept;
    uint64_t collect() noexcept;
    uint64_t get_smooth_interval_us() const noexcept;
    float get_instant_fps() const noexcept;
    bool is_high_refresh() const noexcept;
    bool is_stable() const noexcept { return valid_frame_count_ >= 5; }
    void reset() noexcept;
    
private:
    static constexpr int BUFFER_SIZE = 12;
    std::array<uint64_t, BUFFER_SIZE> frame_intervals_{};
    int write_idx_{0};
    int valid_frame_count_{0};
    
    uint64_t ema_interval_us_{16666};
    float ema_alpha_{0.2f};
    
    std::string sf_surface_;
    std::string drm_path_;
    int drm_fd_{-1};      // 预打开的 DRM fd
    int fpsgo_fd_{-1};    // 预打开的 fpsgo fd
    bool has_fpsgo_{false};
    bool has_sf_latency_{false};
    uint64_t last_collect_time_us_{0};
    
    uint64_t collect_surfaceflinger() noexcept;
    uint64_t collect_drm_vblank() noexcept;
    uint64_t collect_fpsgo() noexcept;
    uint64_t collect_fallback() noexcept;
    static uint64_t parse_drm_timestamp(const std::string& line) noexcept;
};

} // namespace hp::core