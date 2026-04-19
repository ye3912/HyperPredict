#include "core/logger.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#else
// Linux 模拟 Android 日志接口
#include <cstdio>
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
typedef int android_LogPriority;
static int fake_android_log_vprint(int prio, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[%s] ", tag);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
    return 0;
}
#define __android_log_vprint fake_android_log_vprint
#endif

namespace hp {

static LogLevel g_level = LogLevel::INFO;
static const char* g_tag = "HyperPredict";
static FILE* g_file = nullptr;

// 批量写入缓冲区
static char g_buf[8192];
static size_t g_buf_pos = 0;
static int64_t g_last_flush_ms = 0;

static int64_t get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

static void flush_buffer() {
    if (g_buf_pos > 0 && g_file) {
        fwrite(g_buf, 1, g_buf_pos, g_file);
        fflush(g_file);
        g_buf_pos = 0;
    }
}

static void ensure_log_dir(const char* log_path) {
    // 提取日志文件所在的目录路径
    char dir_path[512];
    const char* last_slash = strrchr(log_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - log_path;
        if (dir_len > 0 && dir_len < sizeof(dir_path)) {
            strncpy(dir_path, log_path, dir_len);
            dir_path[dir_len] = '\0';
            // 创建目录（如果不存在）
            mkdir(dir_path, 0755);
        }
    }
}

void init_logger(const char* tag, LogLevel level, const char* log_path) {
    g_tag = tag;
    g_level = level;

    // 如果没有指定日志路径，使用默认路径
    const char* default_log_path = "/data/adb/modules/hyperpredict/logs/hp.log";
    const char* actual_log_path = log_path ? log_path : default_log_path;

    // 确保日志目录存在
    ensure_log_dir(actual_log_path);

    // 打开日志文件（追加模式）
    g_file = fopen(actual_log_path, "a");
    if (g_file) {
        // 使用全缓冲，配合批量写入
        setvbuf(g_file, nullptr, _IOFBF, 8192);
    }
    g_last_flush_ms = get_time_ms();
}

void log_message(LogLevel level, const char* fmt, ...) {
    if (level < g_level) return;
    
    va_list args;
    va_start(args, fmt);
    
    // Android logcat
    android_LogPriority prio = ANDROID_LOG_INFO;
    switch (level) {
        case LogLevel::DEBUG: prio = ANDROID_LOG_DEBUG; break;
        case LogLevel::INFO:  prio = ANDROID_LOG_INFO;  break;
        case LogLevel::WARN:  prio = ANDROID_LOG_WARN;  break;
        case LogLevel::ERROR: prio = ANDROID_LOG_ERROR; break;
    }
    __android_log_vprint(prio, g_tag, fmt, args);
    
    // 优化: 批量写入，500ms 刷新一次
    if (g_file) {
        int len = vsnprintf(g_buf + g_buf_pos, sizeof(g_buf) - g_buf_pos, fmt, args);
        if (len > 0 && g_buf_pos + len < sizeof(g_buf)) {
            g_buf_pos += len;
            g_buf[g_buf_pos++] = '\n';
        }
        
        int64_t now = get_time_ms();
        if (now - g_last_flush_ms >= 500 || level >= LogLevel::ERROR) {
            flush_buffer();
            g_last_flush_ms = now;
        }
    }
    
    va_end(args);
}

void close_logger() {
    flush_buffer();
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

} // namespace hp