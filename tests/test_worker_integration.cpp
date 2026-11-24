#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>

#include "TestServer.hpp"
#include "WorkerClient.hpp"

class WorkerClientMsgIntegrationTest : public ::testing::Test {
protected:
    static std::unique_ptr<TestServer> server;
    std::unique_ptr<WorkerClient> client;

    static void SetUpTestSuite() {
        server = std::make_unique<TestServer>(60020);
        server->Start();
    }

    static void TearDownTestSuite() {
        server.reset();
    }

    void SetUp() override {
        server->ResetFlags();
        client = std::make_unique<WorkerClient>();

        client->Connect("127.0.0.1", 60020);
    }

    void TearDown() override {
        client.reset();
    }
};

TEST_F(WorkerClientMsgIntegrationTest, WorkerSendHelloMessageTest) {
    bool helloReceived = server->WaitForHello(std::chrono::seconds(2));
    EXPECT_TRUE(helloReceived);
}

TEST_F(WorkerClientMsgIntegrationTest, WorkerProcessesTaskAndSendsTaskDone) {
    bool helloReceived = server->WaitForHello(std::chrono::seconds(2));
    ASSERT_TRUE(helloReceived);

    // Create temp file
    std::ofstream file;
    file.open("test_file.log");
    file.close();

    LogSystem::TaskPayload dummyTask = {};
    
    std::string filename = "test_file.log";

    strncpy(dummyTask.filename, filename.c_str(), sizeof(dummyTask.filename) - 1);
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    
    dummyTask.start_offset = 0;
    dummyTask.end_offset = 1000;
    dummyTask.search_id = 100;
    dummyTask.task_id = 100;

    server->SendTask(dummyTask);

    bool taskDone = server->WaitForTaskDone(std::chrono::seconds(2));
    ASSERT_TRUE(taskDone);

    bool taskIdMatch = server->ReceivedTaskDoneIdMatch();
    EXPECT_TRUE(taskIdMatch);

    // Empty file: no values and no rejected lines
    auto batch = server->GetReceivedBatch();
    EXPECT_EQ(batch.stats.count, 0u);
    EXPECT_EQ(batch.stats.errors, 0u);
    
    std::filesystem::remove(filename);
}

TEST_F(WorkerClientMsgIntegrationTest, WorkerComputesStatsAndSendsTaskDone) {
    bool helloReceived = server->WaitForHello(std::chrono::seconds(2));
    ASSERT_TRUE(helloReceived);
    
    std::ofstream file;
    file.open("test_file.log");

    // Three telemetry lines and one line the parser rejects; the last line has no '\n'
    file << "1,946684823847,channel_60,0.7188446\n"
            "2,946684853847,channel_60,0.7197575\n"
            "not a telemetry line\n"
            "3,946684883847,channel_60,0.7307469";
    file.close();

    LogSystem::TaskPayload dummyTask = {};
    
    std::string filename = "test_file.log";

    strncpy(dummyTask.filename, filename.c_str(), sizeof(dummyTask.filename) - 1);
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    
    dummyTask.start_offset = 0;
    dummyTask.end_offset = 1000;
    dummyTask.search_id = 1;
    dummyTask.task_id = 1;

    server->SendTask(dummyTask);

    bool taskDone = server->WaitForTaskDone(std::chrono::seconds(2));
    ASSERT_TRUE(taskDone);

    // Values scaled by 10^kValueScaleDigits (10^10)
    auto batch = server->GetReceivedBatch();
    EXPECT_EQ(batch.task_id, 1u);
    EXPECT_EQ(batch.stats.count, 3u);
    EXPECT_EQ(batch.stats.errors, 1u);
    EXPECT_EQ(batch.stats.sum, 21693490000);  // 7188446000 + 7197575000 + 7307469000
    EXPECT_EQ(batch.stats.min, 7188446000);
    EXPECT_EQ(batch.stats.max, 7307469000);
    EXPECT_FALSE(batch.stats.overflow);
    
    std::filesystem::remove("test_file.log");
}

std::unique_ptr<TestServer> WorkerClientMsgIntegrationTest::server = nullptr;