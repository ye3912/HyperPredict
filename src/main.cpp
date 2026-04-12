cat > src/main.cpp << 'EOF'
#include <csignal>
#include <unistd.h>
#include "core/event_loop.h"
#include "core/logger.h"

volatile sig_atomic_t g_run = 1;
void sig_handler(int) { g_run = 0; }

int main() {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    
    hp::init_logger("HyperPredict", hp::LogLevel::INFO);
    LOGI("HyperPredict v2.0.0 [DEVICE-ADAPTED + POWER-MODEL]");
    
    hp::EventLoop loop;
    
    std::thread monitor([&]() {
        while(g_run) {
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
EOF
