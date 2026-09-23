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
    std::string keyword = "non-existing word";

    strncpy(dummyTask.filename, filename.c_str(), sizeof(dummyTask.filename) - 1);
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    strncpy(dummyTask.keyword, keyword.c_str(), sizeof(dummyTask.keyword) - 1);
    dummyTask.keyword[sizeof(dummyTask.keyword) - 1] = '\0';
    
    dummyTask.start_offset = 0;
    dummyTask.end_offset = 1000;
    dummyTask.search_id = 100;
    dummyTask.task_id = 100;
    dummyTask.max_results = 10000;

    server->SendTask(dummyTask);

    bool taskDone = server->WaitForTaskDone(std::chrono::seconds(2));
    ASSERT_TRUE(taskDone);

    bool taskIdMatch = server->ReceivedTaskDoneIdMatch();
    EXPECT_TRUE(taskIdMatch);

    auto batch = server->GetReceivedBatch();
    EXPECT_TRUE(batch.lines.empty());
    EXPECT_EQ(batch.total_matches, 0);
    
    std::filesystem::remove(filename);
}

TEST_F(WorkerClientMsgIntegrationTest, WorkerFindsLinesAndSendsBatchTest) {
    bool helloReceived = server->WaitForHello(std::chrono::seconds(2));
    ASSERT_TRUE(helloReceived);
    
    std::ofstream file;
    file.open("test_file.log");

    file << "LINE 1\nLINE 2\nERROR LINE\nLINE 3";
    file.close();

    LogSystem::TaskPayload dummyTask = {};
    
    std::string filename = "test_file.log";
    std::string keyword = "ERROR";

    strncpy(dummyTask.filename, filename.c_str(), sizeof(dummyTask.filename) - 1);
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    strncpy(dummyTask.keyword, keyword.c_str(), sizeof(dummyTask.keyword) - 1);
    dummyTask.keyword[sizeof(dummyTask.keyword) - 1] = '\0';
    
    dummyTask.start_offset = 0;
    dummyTask.end_offset = 1000;
    dummyTask.search_id = 1;
    dummyTask.task_id = 1;
    dummyTask.max_results = 10000;

    server->SendTask(dummyTask);

    bool taskDone = server->WaitForTaskDone(std::chrono::seconds(2));
    ASSERT_TRUE(taskDone);

    auto batch = server->GetReceivedBatch();
    EXPECT_EQ(batch.total_matches, 1);
    EXPECT_EQ(batch.lines_found, 1);
    ASSERT_EQ(batch.lines.size(), 1);
    EXPECT_EQ(batch.lines[0], "ERROR LINE");
    
    std::filesystem::remove("test_file.log");
}

std::unique_ptr<TestServer> WorkerClientMsgIntegrationTest::server = nullptr;