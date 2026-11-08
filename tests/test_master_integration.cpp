#include "MasterServer.hpp"
#include "FakeWorker.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

class MasterIntegrationTest : public ::testing::Test {
protected:    
    void SetUp() override {
        m_path = std::filesystem::temp_directory_path() / "loggrid_master_test";
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path/"in");
        std::filesystem::create_directories(m_path/"out");

        std::ofstream logFile(m_path/"in"/"a.log");
        logFile << "ERROR\n";

        // Set base directories
        setenv("LOGGRID_INPUT_DIR", (m_path/"in").string().c_str(), 1);
        setenv("LOGGRID_OUTPUT_DIR", (m_path/"out").string().c_str(), 1);

        // Create master
        m_master = std::make_unique<MasterServer>(port);
        ASSERT_TRUE(m_master->Start());

        m_thread = std::jthread([this](std::stop_token stop) {
            while (true) {
                if (stop.stop_requested()) break;
                m_master->Update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    void TearDown() override {
        m_thread.request_stop();
        if (m_thread.joinable()) m_thread.join();

        if (m_master) m_master->Stop();
        m_master.reset();

        std::filesystem::remove_all(m_path);
    }

protected:
    std::unique_ptr<MasterServer> m_master;
    std::filesystem::path m_path;
    static constexpr uint16_t port{60610};
    std::jthread m_thread;      
};