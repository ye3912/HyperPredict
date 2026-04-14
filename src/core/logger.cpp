#include "core/logger.h"
#include <android/log.h>
#include <cstdarg>
#include <cstdio>

namespace hp {

static const char* s_tag = "HyperPredict";
static LogLevel s_level = LogLevel::DEBUG;

void init_logger(const char* tag, LogLevel level) {
    s_tag = tag;
    s_level = level;
    __android_log_print(ANDROID_LOG_DEBUG, s_tag, "Logger initialized (level=%d)", static_cast<int>(s_level));
}

void log(LogLevel lvl, const char* fmt, ...) {
    if (static_cast<int>(lvl) < static_cast<int>(s_level)) {
        return;
    }
    
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(static_cast<android_LogPriority>(lvl), s_tag, fmt, args);
    va_end(args);
}

} // namespace hp