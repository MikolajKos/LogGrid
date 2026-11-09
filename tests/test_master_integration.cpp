#include "MasterServer.hpp"
#include "FakeWorker.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
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

    /**
     * @brief Creates an input log file of at least the given size, made of matching lines.
     *
     * Used to make the Master split a file into several chunks.
     */
    void WriteLogFile(const std::string& name, uint64_t minBytes) {
        const std::string line = "ERROR line\n";
        std::string block;
        while (block.size() < 1024 * 1024) block += line;

        std::ofstream file(m_path / "in" / name, std::ios::binary);
        uint64_t written = 0;
        while (written < minBytes) {
            file << block;
            written += block.size();
        }
    }

protected:
    // Must match CHUNK_SIZE in NetServer/src/MasterServer.cpp
    static constexpr uint64_t kChunkSize = 5 * 1024 * 1024;

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
TEST_F(MasterIntegrationTest, DispatchesAsManyTasksAsFreeThreads) {
    // 3 chunks, but the Worker reports only 2 free threads
    WriteLogFile("big.log", 2 * kChunkSize + 1);

    FakeWorker worker;
    ASSERT_TRUE(worker.Connect("127.0.0.1", port));
    worker.SendHello(2);

    m_master->StartSearch("big.log", "ERROR", {});

    auto first = worker.ReceiveTask(std::chrono::milliseconds(2000));
    auto second = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(first->start_offset, second->start_offset);

    // The third chunk must wait until one of the threads is free
    EXPECT_FALSE(worker.ReceiveTask(std::chrono::milliseconds(300)));
}

TEST_F(MasterIntegrationTest, FinishesOnlyAfterAllChunksAreDone) {
    // 2 chunks
    WriteLogFile("two_chunks.log", kChunkSize + 1);

    FakeWorker worker;
    ASSERT_TRUE(worker.Connect("127.0.0.1", port));
    worker.SendHello(1);

    uint64_t searchId = m_master->StartSearch("two_chunks.log", "ERROR", {});

    auto first = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(first);
    worker.SendTaskDone(*first);

    // The second chunk is dispatched only after the first one is done
    auto second = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(second);

    auto status = m_master->GetStatus(searchId);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->state, SearchState::Running);
    EXPECT_EQ(status->chunks_total, 2);

    worker.SendTaskDone(*second);
    ASSERT_TRUE(WaitUntilDone(searchId, std::chrono::milliseconds(5000)));

    status = m_master->GetStatus(searchId);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->chunks_done, 2);
}

TEST_F(MasterIntegrationTest, CountsReassignedChunkOnce) {
    FakeWorker workerA;
    FakeWorker workerB;
    ASSERT_TRUE(workerA.Connect("127.0.0.1", port));
    ASSERT_TRUE(workerB.Connect("127.0.0.1", port));

    workerA.SendHello(1);
    uint64_t searchId = m_master->StartSearch("a.log", "ERROR", {});

    auto aTask = workerA.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(aTask);

    workerB.SendHello(1);
    workerA.Crash();

    auto bTask = workerB.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(bTask);
    workerB.SendTaskDone(*bTask);
    ASSERT_TRUE(WaitUntilDone(searchId, std::chrono::milliseconds(5000)));

    auto status = m_master->GetStatus(searchId);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->chunks_total, 1);
    EXPECT_EQ(status->chunks_done, 1);
}

TEST_F(MasterIntegrationTest, ReassignsTaskToWorkerConnectedAfterCrash) {
    FakeWorker workerA;
    ASSERT_TRUE(workerA.Connect("127.0.0.1", port));
    workerA.SendHello(1);

    uint64_t searchId = m_master->StartSearch("a.log", "ERROR", {});

    auto aTask = workerA.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(aTask);

    // The only Worker crashes; the chunk must wait for the next one
    workerA.Crash();

    FakeWorker workerB;
    ASSERT_TRUE(workerB.Connect("127.0.0.1", port));
    workerB.SendHello(1);

    auto bTask = workerB.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(bTask);
    EXPECT_EQ(bTask->start_offset, aTask->start_offset);
    EXPECT_EQ(bTask->end_offset, aTask->end_offset);
    EXPECT_STREQ(bTask->filename, aTask->filename);

    workerB.SendTaskDone(*bTask);
    ASSERT_TRUE(WaitUntilDone(searchId, std::chrono::milliseconds(5000)));
}

TEST_F(MasterIntegrationTest, IgnoresUnknownTaskDone) {
    FakeWorker worker;
    ASSERT_TRUE(worker.Connect("127.0.0.1", port));
    worker.SendHello(1);

    uint64_t searchId = m_master->StartSearch("a.log", "ERROR", {});

    auto task = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(task);

    LogSystem::TaskPayload unknown = *task;
    unknown.task_id += 1000;

    // Messages on one connection are processed in order, so the unknown
    // TaskDone is handled before the real one
    worker.SendTaskDone(unknown);
    worker.SendTaskDone(*task);
    ASSERT_TRUE(WaitUntilDone(searchId, std::chrono::milliseconds(5000)));

    auto status = m_master->GetStatus(searchId);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->chunks_done, 1);
    EXPECT_EQ(status->chunks_total, 1);
}

TEST_F(MasterIntegrationTest, IgnoresDuplicateTaskDone) {
    FakeWorker worker;
    ASSERT_TRUE(worker.Connect("127.0.0.1", port));
    worker.SendHello(1);

    uint64_t firstSearch = m_master->StartSearch("a.log", "ERROR", {});

    auto task = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(task);
    worker.SendTaskDone(*task);
    worker.SendTaskDone(*task);

    // A second search acts as an ordering probe: its TaskDone travels on the
    // same connection after the duplicate, so once it completes, the
    // duplicate has been handled
    uint64_t secondSearch = m_master->StartSearch("a.log", "ERROR", {});
    auto probe = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(probe);
    worker.SendTaskDone(*probe);
    ASSERT_TRUE(WaitUntilDone(secondSearch, std::chrono::milliseconds(5000)));

    auto status = m_master->GetStatus(firstSearch);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->state, SearchState::Done);
    EXPECT_EQ(status->chunks_done, 1);
}

// Regression test for server member destruction order: the io_context must
// outlive connections. Fails only under ASan (LOGGRID_SANITIZER=address).
TEST_F(MasterIntegrationTest, ShutsDownWithConnectedWorker) {
    FakeWorker worker;
    ASSERT_TRUE(worker.Connect("127.0.0.1", port));
    worker.SendHello(1);

    m_master->StartSearch("a.log", "ERROR", {});

    auto receivedOpt = worker.ReceiveTask(std::chrono::milliseconds(2000));
    ASSERT_TRUE(receivedOpt);

    m_thread.request_stop();
    if (m_thread.joinable()) m_thread.join();
    m_master->Stop();
    m_master.reset();
}