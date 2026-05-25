#include <chrono>
#include <iostream>
#include <thread>

#include "MasterServer.hpp"

using namespace std::literals::chrono_literals;

int main() {
    std::cout << "[MASTER] Starting the server...\n";

    MasterServer server(60000);
    
    server.Start();
    std::cout << "[MASTER] The server is running and waiting for Workers\n";
    
    server.StartSearch("../simulation.log", "P4");
    std::cout << "[MASTER] Creating tasks...\n";

    while(true) {
        server.Update();
        std::this_thread::sleep_for(1ms);
    }

    return 0;
}