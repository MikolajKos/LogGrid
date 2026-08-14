#include <chrono>
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

std::unique_ptr<TestServer> WorkerClientMsgIntegrationTest::server = nullptr;