#include <chrono>
#include <iostream>
#include <thread>

#include "HttpApi.hpp"
#include "MasterServer.hpp"

using namespace std::literals::chrono_literals;

int main() {
    std::cout << "[MASTER] Starting the server...\n";

    MasterServer server(60000);
    
    server.Start();
    std::cout << "[MASTER] The server is running and waiting for Workers\n";
    
    HttpApi api(server);
    api.Start(8080);

    while (true) {
        server.Update();
        std::this_thread::sleep_for(1ms);
    }

    return 0;
}