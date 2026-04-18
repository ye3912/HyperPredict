#include "core/frame_pacer.h"
#include "core/logger.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <unistd.h>
#include <cstdlib>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

namespace hp::core {

FramePacer::FramePacer() noexcept 
    : sf_surface_("com.android.systemui")
    , drm_fd_{-1}
    , fpsgo_fd_{-1}
    , has_fpsgo_{false}
    , has_sf_latency_{false}
    , last_collect_time_us_{0}
    , last_finish_ns_{0}
    , last_valid_interval_{16666}
    , sf_socket_{-1}  // socket 连接
    , sf_buffer_pos_{0} {
    // 预分配缓冲区
    sf_buffer_ = new char[4096];
}

FramePacer::~FramePacer() noexcept {
    delete[] sf_buffer_;
    if (sf_socket_ >= 0) close(sf_socket_);
}

bool FramePacer::init() noexcept {
    // 1. DRM vblank (最快路径)
    const char* drm_paths[] = {
        "/sys/class/drm/card0/device/drm/card0-card0-eDP-1/vblank",
        "/sys/class/drm/card0/vblank",
        "/sys/devices/virtual/drm/card0/vblank",
        "/sys/class/drm/card0/card0-DSI-1/vblank"
    };
    for (auto path : drm_paths) {
        if (access(path, R_OK) == 0) {
            drm_path_ = path;
            drm_fd_ = ::open(path, O_RDONLY | O_CLOEXEC);
            break;
        }
    }
    
    // 2. FPSGO (第二快)
    if (access("/sys/devices/virtual/misc/fpsgo/fps", R_OK) == 0) {
        fpsgo_fd_ = ::open("/sys/devices/virtual/misc/fpsgo/fps", O_RDONLY | O_CLOEXEC);
        has_fpsgo_ = fpsgo_fd_ >= 0;
    }
    
    // 3. SurfaceFlinger (最慢，使用 socket 替代 fork)
    init_surfaceflinger_socket();
    
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        last_collect_time_us_ = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
    }
    
    return true;
}

// ============================================================================
// 优化: 使用 socket 替代 fork/exec 获取 SurfaceFlinger 数据
// ============================================================================
void FramePacer::init_surfaceflinger_socket() noexcept {
    // Android SurfaceFlinger 支持通过 socket 通信
    // 路径: /dev/socket/surfaceflinger 或直接通过 service call
    // 
    // 由于 socket 方式需要 native service，这里使用优化策略:
    // 1. 仅在启动时 fork 一次获取 surface 名称
    // 2. 之后使用 /proc/<pid>/fd 监控
    
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            execlp("dumpsys", "dumpsys", "SurfaceFlinger", "--list", (char*)nullptr);
            _exit(1);
        } else if (pid > 0) {
            close(pipefd[1]);
            FILE* fp = fdopen(pipefd[0], "r");
            if (fp) {
                char buf[512];
                while (fgets(buf, sizeof(buf), fp)) {
                    buf[strcspn(buf, "\r\n")] = 0;
                    if (strstr(buf, "SurfaceView") || strstr(buf, "BLAST")) {
                        sf_surface_ = buf;
                        break;
                    }
                }
                fclose(fp);
            }
            close(pipefd[0]);
            int status;
            waitpid(pid, &status, 0);
            has_sf_latency_ = true;
        }
    }
}

uint64_t FramePacer::collect() noexcept {
    uint64_t interval_us = 0;
    
    // 优先级: FPSGO > DRM > SurfaceFlinger > Fallback
    if (has_fpsgo_) {
        interval_us = collect_fpsgo();
    }
    
    if (interval_us == 0 && drm_fd_ >= 0) {
        interval_us = collect_drm_vblank();
    }
    
    // SurfaceFlinger 太慢，跳过直接采集，使用历史值
    // 注意: has_sf_latency_ 仍为 true，但 collect_surfaceflinger() 被跳过
    // 这样可以避免每次 fork/exec
    
    if (interval_us == 0) {
        interval_us = collect_fallback();
    }
    
    // 验证间隔合理性
    if (interval_us < 2000 || interval_us > 100000) {
        return last_valid_interval_;  // 返回上次的有效值
    }
    
    last_valid_interval_ = interval_us;
    
    // 缓存到循环缓冲区
    frame_intervals_[write_idx_] = interval_us;
    write_idx_ = (write_idx_ + 1) % BUFFER_SIZE;
    if (valid_frame_count_ < BUFFER_SIZE) valid_frame_count_++;
    
    // EMA 平滑
    ema_interval_us_ = static_cast<uint64_t>(
        ema_interval_us_ * (1.0f - ema_alpha_) + interval_us * ema_alpha_
    );
    
    return interval_us;
}

// 优化: 完全跳过 SurfaceFlinger fork，直接使用 DRM/FPSGO/历史值
uint64_t FramePacer::collect_surfaceflinger() noexcept {
    // 不再每次 fork，直接返回 0 让 collect() 使用 fallback
    return 0;
}

uint64_t FramePacer::collect_drm_vblank() noexcept {
    if (drm_fd_ < 0) return 0;
    
    char line[256] = {0};
    ssize_t n = pread(drm_fd_, line, sizeof(line) - 1, 0);
    if (n > 0) {
        line[n] = '\0';
        
        uint64_t ts = parse_drm_timestamp(line);
        if (ts > last_finish_ns_) {
            uint64_t delta_us = (ts - last_finish_ns_) / 1000;
            last_finish_ns_ = ts;
            if (delta_us >= 8000 && delta_us <= 100000) {
                return delta_us;
            }
        }
    }
    
    return 0;
}

uint64_t FramePacer::collect_fpsgo() noexcept {
    if (fpsgo_fd_ < 0) return 0;
    
    char line[64] = {0};
    ssize_t n = pread(fpsgo_fd_, line, sizeof(line) - 1, 0);
    if (n > 0) {
        line[n] = '\0';
        float fps = 0.0f;
        if (sscanf(line, "%f", &fps) == 1) {
            if (fps > 20.0f && fps < 144.0f) {
                return static_cast<uint64_t>(1000000.0f / fps);
            }
        }
    }
    
    return 0;
}

uint64_t FramePacer::collect_fallback() noexcept {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return last_valid_interval_;
    }
    
    uint64_t now_us = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
    uint64_t delta = now_us - last_collect_time_us_;
    last_collect_time_us_ = now_us;
    
    if (delta >= 8000 && delta <= 100000) {
        return delta;
    }
    
    return last_valid_interval_;
}

uint64_t FramePacer::parse_drm_timestamp(const std::string& line) noexcept {
    size_t pos = line.find("timestamp:");
    if (pos != std::string::npos) {
        const char* num_start = line.c_str() + pos + 10;
        char* end = nullptr;
        unsigned long val = std::strtoul(num_start, &end, 10);
        if (end != num_start) {
            return static_cast<uint64_t>(val);
        }
    }
    return 0;
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
    last_valid_interval_ = 16666;
    last_finish_ns_ = 0;
}

} // namespace hp::core
