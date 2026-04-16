#include "core/frame_pacer.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <unistd.h>  // ✅ 新增：用于 access() 和 R_OK
#include <cinttypes> // ✅ 新增：用于 PRIu64

namespace hp::core {

FramePacer::FramePacer() noexcept 
    : sf_surface_("com.android.systemui")
    , drm_path_{}
    , has_fpsgo_{false}
    , has_sf_latency_{false}
    , last_collect_time_us_{0} {
}

bool FramePacer::init() noexcept {
    // 1. 检测 SurfaceFlinger
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
        LOGI("FramePacer: SurfaceFlinger available, target=%s", sf_surface_.c_str());
    }
    
    // 2. 检测 DRM vblank
    const char* drm_paths[] = {
        "/sys/class/drm/card0/device/drm/card0-card0-eDP-1/vblank",
        "/sys/class/drm/card0/vblank",
        "/sys/devices/virtual/drm/card0/vblank",
        "/sys/class/drm/card0/card0-DSI-1/vblank"
    };
    for (auto path : drm_paths) {
        if (access(path, R_OK) == 0) { // R_OK 已解决
            drm_path_ = path;
            LOGI("FramePacer: DRM vblank at %s", path);
            break;
        }    }
    
    // 3. 检测 MTK FPSGo
    if (access("/sys/devices/virtual/misc/fpsgo/fps", R_OK) == 0) { // R_OK 已解决
        has_fpsgo_ = true;
        LOGI("FramePacer: MTK FPSGo available");
    }
    
    // 4. 初始化时间戳
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
    return ema_interval_us_;}

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
// ────────── 采集方法 ──────────

uint64_t FramePacer::collect_surfaceflinger() noexcept {
    static uint64_t last_finish_ns = 0;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "dumpsys SurfaceFlinger --latency '%s' 2>/dev/null | tail -n 1",
             sf_surface_.c_str());
    
    FILE* fp = popen(cmd, "r");
    if (!fp) return 0;
    
    char line[256] = {0};
    if (fgets(line, sizeof(line), fp)) {
        pclose(fp);
        
        uint64_t start_ns = 0, finish_ns = 0, flags = 0;
        // ✅ 修复：使用 PRIu64 替代 %llu
        if (sscanf(line, "%" PRIu64 " %" PRIu64 " %" PRIu64, &start_ns, &finish_ns, &flags) >= 2) {
            if (finish_ns > 0 && finish_ns > last_finish_ns) {
                uint64_t delta_us = (finish_ns - last_finish_ns) / 1000;
                last_finish_ns = finish_ns;
                
                if (delta_us >= 8000 && delta_us <= 100000) {
                    return delta_us;
                }
            }
        }
    } else {
        pclose(fp);
    }
    
    return 0;
}

uint64_t FramePacer::collect_drm_vblank() noexcept {
    if (drm_path_.empty()) return 0;
    
    static uint64_t last_vblank_ts = 0;
    
    FILE* fp = fopen(drm_path_.c_str(), "r");
    if (!fp) return 0;
    
    char line[256] = {0};
    if (fgets(line, sizeof(line), fp)) {
        fclose(fp);
        
        uint64_t ts = parse_drm_timestamp(line);
        if (ts > 0 && ts > last_vblank_ts) {
            uint64_t delta_us = (ts - last_vblank_ts) / 1000;            last_vblank_ts = ts;
            
            if (delta_us >= 8000 && delta_us <= 100000) {
                return delta_us;
            }
        }
        last_vblank_ts = ts;
    } else {
        fclose(fp);
    }
    
    return 0;
}

uint64_t FramePacer::collect_fpsgo() noexcept {
    FILE* fp = fopen("/sys/devices/virtual/misc/fpsgo/fps", "r");
    if (!fp) return 0;
    
    char line[64] = {0};
    if (fgets(line, sizeof(line), fp)) {
        fclose(fp);
        
        float fps = 0.0f;
        if (sscanf(line, "%f", &fps) == 1) {
            if (fps > 20.0f && fps < 144.0f) {
                return static_cast<uint64_t>(1000000.0f / fps);
            }
        }
    } else {
        fclose(fp);
    }
    
    return 0;
}

uint64_t FramePacer::collect_fallback() noexcept {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 16666;
    }
    
    uint64_t now_us = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
    uint64_t delta = now_us - last_collect_time_us_;
    last_collect_time_us_ = now_us;
    
    if (delta >= 8000 && delta <= 100000) {
        return delta;
    }
    
    return 16666;}

uint64_t FramePacer::parse_drm_timestamp(const std::string& line) noexcept {
    size_t pos = line.find("timestamp:");
    if (pos != std::string::npos) {
        // ✅ 修复：移除 try-catch，因为 NDK 禁用了异常
        // 简单解析，只取数字
        const char* num_start = line.c_str() + pos + 10;
        char* end = nullptr;
        unsigned long long val = std::strtoull(num_start, &end, 10);
        if (end != num_start) {
            return static_cast<uint64_t>(val);
        }
    }
    return 0;
}

} // namespace hp::core