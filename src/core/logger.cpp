#include "core/logger.h"
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

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

// 可靠的目录创建 (使用 mkdir 系统调用)
static int ensure_log_dir(const char* log_path) {
    char dir_path[512];
    const char* last_slash = strrchr(log_path, '/');
    if (!last_slash) return -1;
    
    size_t dir_len = last_slash - log_path;
    if (dir_len == 0 || dir_len >= sizeof(dir_path)) return -1;
    
    strncpy(dir_path, log_path, dir_len);
    dir_path[dir_len] = '\0';
    
    // 逐级创建目录
    for (size_t i = 1; i <= dir_len; i++) {
        if (dir_path[i] == '/') {
            char old_char = dir_path[i];
            dir_path[i] = '\0';
            mkdir(dir_path, 0755);  // 忽略错误，继续
            dir_path[i] = old_char;
        }
    }
    
    // 最终创建完整目录
    return mkdir(dir_path, 0755);
}

void init_logger(const char* tag, LogLevel level, const char* log_path) {
    g_tag = tag;
    g_level = level;

    // 立即输出到 stderr (最可靠)
    fprintf(stderr, "[HyperPredict] Logger Init Start\n");
    fflush(stderr);

    // 立即输出到 logcat (确保能看到初始化信息)
    #ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, tag, "=== Logger Init Start ===");
    #else
    // Linux 测试环境
    printf("[HyperPredict] Logger Init Start\n");
    #endif

    // 默认路径：模块目录下
    const char* default_log_path = "/data/local/tmp/hp.log";
    const char* actual_log_path = log_path ? log_path : default_log_path;

    // 如果路径不以/tmp或logs开头，使用默认路径（Magisk模块可能没有/data/adb权限）
    if (log_path && 
        strstr(log_path, "/data/local/tmp") == nullptr && 
        strstr(log_path, "logs/") == nullptr &&
        strstr(log_path, "/sdcard") == nullptr) {
        actual_log_path = default_log_path;
    }

    // 确保日志目录存在
    ensure_log_dir(actual_log_path);

    // 打开日志文件（追加模式）
    g_file = fopen(actual_log_path, "a");
    if (g_file) {
        // 使用全缓冲，配合批量写入
        setvbuf(g_file, nullptr, _IOFBF, 8192);
        // 写入初始化日志
        fprintf(g_file, "\n=== HyperPredict Logger Initialized ===\n");
        fprintf(g_file, "Log path: %s\n", actual_log_path);
        fprintf(g_file, "Log level: %d\n", static_cast<int>(level));
        fflush(g_file);
        
        // logcat 输出
        #ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO, tag, "Log initialized: path=%s level=%d", actual_log_path, static_cast<int>(level));
        #endif
    } else {
        // 文件打开失败，输出到 stderr 和 logcat
        fprintf(stderr, "[ERROR] Failed to open log file: %s\n", actual_log_path);
        fprintf(stderr, "[ERROR] errno: %d (%s)\n", errno, strerror(errno));
        
        #ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, tag, "Log init FAILED: path=%s errno=%d", actual_log_path, errno);
        #endif
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