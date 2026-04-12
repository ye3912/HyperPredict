#include "core/logger.h"
#include <cstdarg>
#include <cstdio>

namespace hp {
static const char* s_tag = "HyperPredict";
static LogLevel s_level = LogLevel::INFO;

void init(const char* tag, LogLevel level) {
    s_tag = tag;
    s_level = level;
}

void log(LogLevel lvl, const char* fmt, ...) {
    if(lvl < s_level) return;
    va_list args; va_start(args, fmt);
    __android_log_vprint(static_cast<android_LogPriority>(lvl), s_tag, fmt, args);
    va_end(args);
}
}