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

namespace hp::core {

FramePacer::FramePacer() noexcept 
    : sf_surface_("com.android.systemui")
    , drm_fd_{-1}
    , fpsgo_fd_{-1}
    , has_fpsgo_{false}
    , has_sf_latency_{false}
    , last_collect_time_us_{0} {
}

bool FramePacer::init() noexcept {
    // 优化: 使用预打开的管道替代 popen，避免每次 fork
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
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
    
    const char* drm_paths[] = {
        "/sys/class/drm/card0/device/drm/card0-card0-eDP-1/vblank",
        "/sys/class/drm/card0/vblank",
        "/sys/devices/virtual/drm/card0/vblank",
        "/sys/class/drm/card0/card0-DSI-1/vblank"
    };
    for (auto path : drm_paths) {
        if (access(path, R_OK) == 0) {
            drm_path_ = path;
            drm_fd_ = ::open(path, O_RDONLY | O_CLOEXEC);  // 预打开
            break;
        }
    }
    
    if (access("/sys/devices/virtual/misc/fpsgo/fps", R_OK) == 0) {
        fpsgo_fd_ = ::open("/sys/devices/virtual/misc/fpsgo/fps", O_RDONLY | O_CLOEXEC);
        has_fpsgo_ = fpsgo_fd_ >= 0;
    }
    
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
uint64_t FramePacer::collect_surfaceflinger() noexcept {
    static uint64_t last_finish_ns = 0;
    
    // 优化: 使用 pipe + fork + exec 替代 popen
    int pipefd[2];
    if (pipe(pipefd) != 0) return 0;
    
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("dumpsys", "dumpsys", "SurfaceFlinger", "--latency", 
               sf_surface_.c_str(), (char*)nullptr);
        _exit(1);
    } else if (pid > 0) {
        close(pipefd[1]);
        char line[256] = {0};
        ssize_t n = read(pipefd[0], line, sizeof(line) - 1);
        close(pipefd[0]);
        
        int status;
        waitpid(pid, &status, 0);
        
        if (n > 0) {
            line[n] = '\0';
            // 取最后一行
            char* last_newline = strrchr(line, '\n');
            if (last_newline) {
                last_newline++;
                *last_newline = '\0';
                last_newline = strrchr(line, '\n');
                if (last_newline) last_newline++;
                else last_newline = line;
            } else {
                last_newline = line;
            }
            
            unsigned long start_ns = 0, finish_ns = 0, flags = 0;
            if (sscanf(last_newline, "%lu %lu %lu", &start_ns, &finish_ns, &flags) >= 2) {
                if (finish_ns > 0 && finish_ns > last_finish_ns) {
                    uint64_t delta_us = (finish_ns - last_finish_ns) / 1000;
                    last_finish_ns = finish_ns;
                    if (delta_us >= 8000 && delta_us <= 100000) {
                        return delta_us;
                    }
                }
            }
        }
    }
    
    return 0;
}

uint64_t FramePacer::collect_drm_vblank() noexcept {
    if (drm_fd_ < 0) return 0;
    
    static uint64_t last_vblank_ts = 0;
    
    char line[256] = {0};
    // 优化: 使用 pread 避免 lseek 竞争
    ssize_t n = pread(drm_fd_, line, sizeof(line) - 1, 0);
    if (n > 0) {
        line[n] = '\0';
        
        uint64_t ts = parse_drm_timestamp(line);
        if (ts > 0 && ts > last_vblank_ts) {
            uint64_t delta_us = (ts - last_vblank_ts) / 1000;
            last_vblank_ts = ts;
            if (delta_us >= 8000 && delta_us <= 100000) {
                return delta_us;
            }
        }
        last_vblank_ts = ts;
    }
    
    return 0;
}

uint64_t FramePacer::collect_fpsgo() noexcept {
    if (fpsgo_fd_ < 0) return 0;
    
    char line[64] = {0};
    // 优化: 使用 pread
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
        return 16666;
    }
    
    uint64_t now_us = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
    uint64_t delta = now_us - last_collect_time_us_;
    last_collect_time_us_ = now_us;
    
    if (delta >= 8000 && delta <= 100000) {
        return delta;
    }
    
    return 16666;
}
uint64_t FramePacer::parse_drm_timestamp(const std::string& line) noexcept {
    size_t pos = line.find("timestamp:");
    if (pos != std::string::npos) {
        // ✅ 修复：移除 try-catch，使用 C 风格解析
        const char* num_start = line.c_str() + pos + 10;
        char* end = nullptr;
        unsigned long val = std::strtoul(num_start, &end, 10);
        if (end != num_start) {
            return static_cast<uint64_t>(val);
        }
    }
    return 0;
}

} // namespace hp::core