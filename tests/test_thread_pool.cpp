#include <gtest/gtest.h>
#include <memory>

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
    EXPECT_EQ(pool->GetThreadCount(), GetParam().expectedThreads);
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