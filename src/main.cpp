#include <csignal>
#include <atomic>
#include <unistd.h>
#include <android/log.h>
#include "core/event_loop.h"
#include "core/logger.h"

std::atomic<bool> g_run{true};
hp::EventLoop* g_loop = nullptr;

void sig_handler(int sig) {
    if(sig == SIGUSR1) {
        __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "Manual model save requested");
        if(g_loop) {
            g_loop->save_model();
            g_loop->export_model_json();
        }
        return;
    }
    LOGI("Signal %d received, shutting down...", sig);
    g_run.store(false, std::memory_order_release);
}

int main() {
    // 初始化日志（INFO 级别降低开销）
    hp::init_logger("HyperPredict", hp::LogLevel::INFO);
    
    LOGI("================================");
    LOGI("HyperPredict v2.0.0 Starting...");
    LOGI("================================");
    
    struct sigaction sa{};
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr);  // 手动保存模型
    signal(SIGPIPE, SIG_IGN);
    
    LOGI("Initializing event loop...");
    
    hp::EventLoop loop;
    g_loop = &loop;
    
    LOGI("Starting event loop...");
    loop.start();
    
    LOGI("Shutdown complete");
    return 0;
}