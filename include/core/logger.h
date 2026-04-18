#pragma once

namespace hp {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

void init_logger(const char* tag, LogLevel level, const char* log_path = nullptr);
void log_message(LogLevel level, const char* fmt, ...);
void close_logger();

#define LOGD(...) hp::log_message(hp::LogLevel::DEBUG, __VA_ARGS__)
#define LOGI(...) hp::log_message(hp::LogLevel::INFO, __VA_ARGS__)
#define LOGW(...) hp::log_message(hp::LogLevel::WARN, __VA_ARGS__)
#define LOGE(...) hp::log_message(hp::LogLevel::ERROR, __VA_ARGS__)

} // namespace hp