#include "core/logger.h"
#include <android/log.h>
#include <fstream>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <sys/time.h>

namespace hp {

static LogLevel g_level = LogLevel::INFO;
static const char* g_tag = "HyperPredict";
static FILE* g_file = nullptr;

// 优化: 批量写入缓冲区
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

void init_logger(const char* tag, LogLevel level) {
    g_tag = tag;
    g_level = level;
    
    const char* log_path = "/data/adb/modules/hyperpredict/logs/hp.log";
    g_file = fopen(log_path, "a");
    if (g_file) {
        setbuf(g_file, nullptr);  // 无缓冲，自己管理
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