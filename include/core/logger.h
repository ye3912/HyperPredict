#pragma once
#include <android/log.h>

namespace hp {
enum class LogLevel : int { DEBUG=3, INFO=4, WARN=5, ERROR=6 };
void init(const char* tag, LogLevel level);
void log(LogLevel lvl, const char* fmt, ...);
}

#define LOGD(...) hp::log(hp::LogLevel::DEBUG, __VA_ARGS__)
#define LOGI(...) hp::log(hp::LogLevel::INFO, __VA_ARGS__)
#define LOGW(...) hp::log(hp::LogLevel::WARN, __VA_ARGS__)
#define LOGE(...) hp::log(hp::LogLevel::ERROR, __VA_ARGS__)