#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <vector>

#include "ThreadPool.hpp"

struct ThreadPoolTestData {
    size_t inputThreads;
    size_t expectedThreads;
};

class ThreadPoolParamTest : public ::testing::TestWithParam<ThreadPoolTestData> {
protected:    
    std::unique_ptr<ThreadPool> pool;

    void SetUp() override {
        pool = std::make_unique<ThreadPool>(GetParam().inputThreads);
    }
};

TEST_P(ThreadPoolParamTest, InitializesCorrectNumberOfThreads) {
    EXPECT_EQ(pool->Size(), GetParam().expectedThreads);
}

INSTANTIATE_TEST_SUITE_P(
    VariousPoolSize,
    ThreadPoolParamTest,
    ::testing::Values(
        ThreadPoolTestData{1, 1},
        ThreadPoolTestData{2, 2},
        ThreadPoolTestData{4, 4},
        ThreadPoolTestData{8, 8},
        ThreadPoolTestData{32, 32}
    )
);

// =====================================================================

struct AsyncCounterTestData {
    size_t threadCount;
    int tasksToEnqueue;
};

class ThreadPoolAsyncCountTest : public ::testing::TestWithParam<AsyncCounterTestData> {
protected:
    std::unique_ptr<ThreadPool> pool;
    std::atomic<int> counter{0};

    void SetUp() override {
        pool = std::make_unique<ThreadPool>(GetParam().threadCount);
    }
};

TEST_P(ThreadPoolAsyncCountTest, ExecutesAllTasksCorrectly) {
    int tasks = GetParam().tasksToEnqueue;

    for (int i = 0; i < tasks; ++i) {
        pool->Enqueue([this]() {
            counter++;
        });
    }

    // Wait for threads to join
    pool.reset();

    EXPECT_EQ(counter.load(), tasks);
}

INSTANTIATE_TEST_SUITE_P(
    ThreadCountCases,
    ThreadPoolAsyncCountTest,
    ::testing::Values(
        AsyncCounterTestData{1, 100},
        AsyncCounterTestData{2, 1000},
        AsyncCounterTestData{7, 1000},
        AsyncCounterTestData{13, 5000},
        AsyncCounterTestData{32, 5000},
        AsyncCounterTestData{61, 10000}
    )
);

// =====================================================================

class ThreadPoolFifoTest : public ::testing::Test {
protected:
    std::unique_ptr<ThreadPool> pool;
    std::vector<int> executionOrder;
    std::mutex vectorMutex;

    void SetUp() override {
        // We have to create only one thread to make sure no data races occure in executionOrder
        pool = std::make_unique<ThreadPool>(1);
    }
};

TEST_F(ThreadPoolFifoTest, ExecutesTasksInFifoOrder) {
    std::promise<void> startGate;
    std::future<void> startFuture = startGate.get_future();

    // Enqueue future so task execution in thread will wait for set_value()
    pool->Enqueue([&startFuture]() {
        startFuture.wait();
    });

    // Enqueue tasks in order
    for (int i = 1; i <= 100; ++i) {
        pool->Enqueue([this, i]() {
            std::lock_guard<std::mutex> lock(vectorMutex);
            executionOrder.push_back(i);
        });
    }

    // Wake up thread by calling set_value()
    startGate.set_value();

    pool.reset();

    std::vector<int> expectedOrder;
    for (int i = 1; i <= 100; ++i) expectedOrder.push_back(i);

    EXPECT_EQ(executionOrder, expectedOrder);
}