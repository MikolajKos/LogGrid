#include <chrono>
#include <iostream>
#include <thread>

#include "WorkerClient.hpp"

// #define LOCAL_RUN
#define DOCKER_RUN

#define LOOPBACK "127.0.0.1"
#define DOCKER_MASTER_IP "master-node"

using namespace std::literals::chrono_literals;

int main() {
    WorkerClient worker;

#ifdef DOCKER_RUN
    worker.Connect(DOCKER_MASTER_IP, 60000);
#elif defined(LOCAL_RUN)
    worker.Connect(LOOPBACK, 60000);
#else
    ASSERT("No build enviroment was defined")
#endif

    std::cout << "[WORKER] Connecting to the server...\n";

    // Wait for connection to establish
    int connAttempts = 0;
    while(!worker.IsConnected() && connAttempts < 10) {
        std::this_thread::sleep_for(500ms);
        connAttempts++;
    }
    
    if (worker.IsConnected()) {
        // Connection [ACK]
        worker.SendMessage(LogSystem::LogSearchMsg::Worker_Hello);

        std::cout << "[WORKER] Connection established\n";

        while (worker.IsConnected()) {
            std::this_thread::sleep_for(200ms);
        }
    }

    return 0;
}