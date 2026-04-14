#include <csignal>
#include <atomic>
#include <unistd.h>
#include <android/log.h>
#include "core/event_loop.h"

std::atomic<bool> g_run{true};

void sig_handler(int sig) {
    __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "Signal %d", sig);
    g_run.store(false, std::memory_order_release);
}

int main() {
    __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "================================");
    __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "HyperPredict v2.0.0 Starting...");
    __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "================================");
    
    struct sigaction sa{};
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
    
    __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "Initializing...");
    
    hp::EventLoop loop;
    
    __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "Starting...");
    
    loop.start();
    
    __android_log_print(ANDROID_LOG_INFO, "HyperPredict", "Shutdown complete");
    return 0;
}