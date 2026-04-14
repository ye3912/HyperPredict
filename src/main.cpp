#include <csignal>
#include <unistd.h>
#include "core/event_loop.h"
#include "core/logger.h"

volatile sig_atomic_t g_run = 1;
void sig_handler(int) { g_run = 0; }

int main() {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler);
    signal(SIGPIPE, SIG_IGN);
    
    hp::init_logger("HyperPredict", hp::LogLevel::INFO);
    LOGI("HyperPredict v2.0.0 [DEVICE-ADAPTED + POWER-MODEL]");
    
    hp::EventLoop loop;
    loop.start();
    
    LOGI("Shutdown complete.");
    return 0;
}