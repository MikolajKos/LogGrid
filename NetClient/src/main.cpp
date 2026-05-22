#include <chrono>
#include <iostream>
#include <thread>

#include "WorkerClient.hpp"

#define LOOPBACK "127.0.0.1"

using namespace std::literals::chrono_literals;

int main() {
    WorkerClient worker;

    worker.Connect(LOOPBACK, 60000);
    std::cout << "[WORKER] Connecting to the server...\n";

    // Wait for connection to establish
    int connAttempts = 0;
    while(!worker.IsConnected() && connAttempts < 10) {
        std::this_thread::sleep_for(500ms);
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