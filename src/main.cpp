#include <csignal>
#include <unistd.h>
#include <atomic>
#include "core/event_loop.h"
#include "core/logger.h"

std::atomic<bool> g_run{true};

void sig_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_run.store(false);
    }
}

int main() {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);
    
    hp::init_logger("HyperPredict", hp::LogLevel::INFO);
    LOGI("HyperPredict v2.0.0 [DEVICE-ADAPTED + POWER-MODEL]");
    
    hp::EventLoop loop;
    
    std::thread monitor([&]() {
        while(g_run.load()) {
            if (!loop.is_running()) break;
            sleep(2);
        }
        loop.stop();
    });
    
    loop.start();
    monitor.join();
    
    LOGI("Shutdown complete.");
    return 0;
}
