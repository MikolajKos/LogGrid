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

    LogSystem::TaskPayload dummyTask;
    
    std::string filename = "test_file.log";
    std::string keyword = "non-existing word";

    strncpy(dummyTask.filename, filename.c_str(), sizeof(dummyTask.filename));
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    strncpy(dummyTask.keyword, keyword.c_str(), sizeof(dummyTask.keyword));
    dummyTask.keyword[sizeof(dummyTask.keyword) - 1] = '\0';
    
    dummyTask.start_offset = 0;
    dummyTask.end_offset = 1000;
    dummyTask.search_id = 100;
    dummyTask.task_id = 100;

    server->SendTask(dummyTask);

    bool taskDone = server->WaitForTaskDone(std::chrono::seconds(2));
    ASSERT_TRUE(taskDone);

    bool taskIdMatch = server->ReceivedTaskDoneIdMatch();
    
    EXPECT_TRUE(taskIdMatch);
    
    file.close();
    std::filesystem::remove(filename);
}

std::unique_ptr<TestServer> WorkerClientMsgIntegrationTest::server = nullptr;