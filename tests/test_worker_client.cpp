#include <gtest/gtest.h>
#include <memory>

#include "WorkerClient.hpp"

struct WorkerThreadsTestData {
    size_t inputThreads;
    size_t expectedThreads;
};

class WorkerClientThreadCalcTest : public ::testing::TestWithParam<WorkerThreadsTestData> {
};

TEST_P(WorkerClientThreadCalcTest, CalculatesOptimalThreadCount) {
    size_t input = GetParam().inputThreads;
    size_t expected = GetParam().expectedThreads;

    size_t actual = WorkerClient::CalculateThreadCount(input);

    EXPECT_EQ(expected, actual);
}

INSTANTIATE_TEST_SUITE_P(
    HardwareEdgeCases,
    WorkerClientThreadCalcTest,
    ::testing::Values(
        WorkerThreadsTestData{0, 4},
        WorkerThreadsTestData{1, 1},
        WorkerThreadsTestData{2, 1},
        WorkerThreadsTestData{8, 7},
        WorkerThreadsTestData{32, 31}
    )
);