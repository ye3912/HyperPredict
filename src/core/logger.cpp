#include "core/logger.h"
#include <android/log.h>
#include <fstream>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace hp {

static LogLevel g_level = LogLevel::INFO;
static const char* g_tag = "HyperPredict";
static FILE* g_file = nullptr;

void init_logger(const char* tag, LogLevel level) {
    g_tag = tag;
    g_level = level;
    
    // ✅ 关键：打开文件日志
    const char* log_path = "/data/adb/modules/hyperpredict/logs/hp.log";
    g_file = fopen(log_path, "a");
    if(g_file) {
        setbuf(g_file, nullptr);  // 无缓冲
    }
}

void log_message(LogLevel level, const char* fmt, ...) {
    if(level < g_level) return;
    
    va_list args;
    va_start(args, fmt);
    
    // Android logcat
    android_LogPriority prio = ANDROID_LOG_INFO;
    switch(level) {
        case LogLevel::DEBUG: prio = ANDROID_LOG_DEBUG; break;
        case LogLevel::INFO: prio = ANDROID_LOG_INFO; break;
        case LogLevel::WARN: prio = ANDROID_LOG_WARN; break;
        case LogLevel::ERROR: prio = ANDROID_LOG_ERROR; break;
    }
    __android_log_vprint(prio, g_tag, fmt, args);
    
    // ✅ 文件日志
    if(g_file) {
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, args);
        fprintf(g_file, "%s\n", buf);
        fflush(g_file);
    }
    
    va_end(args);
}

void close_logger() {
    if(g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

} // namespace hp