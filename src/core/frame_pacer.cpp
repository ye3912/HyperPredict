#include "core/frame_pacer.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <unistd.h>      // ✅ 新增：解决 R_OK 未定义
#include <cstdlib>       // ✅ 新增：用于 strtoul (替代 try-catch)

namespace hp::core {

FramePacer::FramePacer() noexcept 
    : sf_surface_("com.android.systemui")
    , drm_path_{}
    , has_fpsgo_{false}
    , has_sf_latency_{false}
    , last_collect_time_us_{0} {
}

bool FramePacer::init() noexcept {
    FILE* fp = popen("dumpsys SurfaceFlinger --list 2>/dev/null", "r");
    if (fp) {
        char buf[512];
        while (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\r\n")] = 0;
            if (strstr(buf, "SurfaceView") || strstr(buf, "BLAST")) {
                sf_surface_ = buf;
                break;
            }
        }
        pclose(fp);
        has_sf_latency_ = true;
    }
    
    const char* drm_paths[] = {
        "/sys/class/drm/card0/device/drm/card0-card0-eDP-1/vblank",
        "/sys/class/drm/card0/vblank",
        "/sys/devices/virtual/drm/card0/vblank",
        "/sys/class/drm/card0/card0-DSI-1/vblank"
    };
    for (auto path : drm_paths) {
        if (access(path, R_OK) == 0) {  // ✅ R_OK 现在已定义
            drm_path_ = path;
            break;
        }
    }
    
    if (access("/sys/devices/virtual/misc/fpsgo/fps", R_OK) == 0) {
        has_fpsgo_ = true;    }
    
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        last_collect_time_us_ = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
    }
    
    return true;
}

uint64_t FramePacer::collect() noexcept {
    uint64_t interval_us = 0;
    
    if (has_fpsgo_) {
        interval_us = collect_fpsgo();
    }
    if (interval_us == 0 && !drm_path_.empty()) {
        interval_us = collect_drm_vblank();
    }
    if (interval_us == 0 && has_sf_latency_) {
        interval_us = collect_surfaceflinger();
    }
    if (interval_us == 0) {
        interval_us = collect_fallback();
    }
    
    if (interval_us < 2000 || interval_us > 100000) {
        return 0;
    }
    
    frame_intervals_[write_idx_] = interval_us;
    write_idx_ = (write_idx_ + 1) % BUFFER_SIZE;
    if (valid_frame_count_ < BUFFER_SIZE) valid_frame_count_++;
    
    ema_interval_us_ = static_cast<uint64_t>(
        ema_interval_us_ * (1.0f - ema_alpha_) + interval_us * ema_alpha_
    );
    
    return interval_us;
}

uint64_t FramePacer::get_smooth_interval_us() const noexcept {
    return ema_interval_us_;
}

float FramePacer::get_instant_fps() const noexcept {
    if (ema_interval_us_ < 2000) return 120.0f;
    return 1000000.0f / static_cast<float>(ema_interval_us_);
}
bool FramePacer::is_high_refresh() const noexcept {
    return get_instant_fps() > 90.0f;
}

void FramePacer::reset() noexcept {
    frame_intervals_.fill(0);
    write_idx_ = 0;
    valid_frame_count_ = 0;
    ema_interval_us_ = 16666;
}

// (接第二段...)