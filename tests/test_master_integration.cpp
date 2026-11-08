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
    bool WaitUntilDone(uint64_t searchId, std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            auto statusOpt = m_master->GetStatus(searchId);
            
            if (statusOpt && statusOpt->state == SearchState::Done) {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        return false;
    }

protected:
    std::unique_ptr<MasterServer> m_master;
    std::filesystem::path m_path;
    static constexpr uint16_t port{60610};
    std::jthread m_thread;      
};

TEST_F(MasterIntegrationTest, CompletesSearchWithOneWorker) {
    FakeWorker worker;
    ASSERT_TRUE(worker.Connect("127.0.0.1", port));
    // Master will register this worker
    worker.SendHello(1);

    // Sends task
    uint64_t searchId = m_master->StartSearch("a.log", "ERROR", {});

    auto receivedOpt = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(receivedOpt);

    worker.SendTaskDone(*receivedOpt);
    
    ASSERT_TRUE(WaitUntilDone(searchId, std::chrono::milliseconds(5000)));
}

TEST_F(MasterIntegrationTest, ReassignsTaskWhenWorkerCrashes) {
    FakeWorker workerA;
    FakeWorker workerB;

    ASSERT_TRUE(workerA.Connect("127.0.0.1", port));
    ASSERT_TRUE(workerB.Connect("127.0.0.1", port));

    workerA.SendHello(1);
    
    uint64_t searchId = m_master->StartSearch("a.log", "ERROR", {});
    
    // Save offset and filename from task for worker B validation
    auto aTaskOpt = workerA.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(aTaskOpt);
    auto aTask = *aTaskOpt;
    
    // Task was assigned to worker A so B is waiting as idle worker
    workerB.SendHello(1);

    workerA.Crash();

    auto bTaskOpt = workerB.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(bTaskOpt);
    auto bTask = *bTaskOpt;

    EXPECT_EQ(bTask.end_offset, aTask.end_offset);
    EXPECT_EQ(bTask.start_offset, aTask.start_offset);
    ASSERT_STREQ(bTask.filename, aTask.filename);

    workerB.SendTaskDone(bTask);

    ASSERT_TRUE(WaitUntilDone(searchId, std::chrono::milliseconds(5000)));
}