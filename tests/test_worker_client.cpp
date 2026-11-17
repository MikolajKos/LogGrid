#include "WorkerClient.hpp"

#include <gtest/gtest.h>


struct WorkerThreadsTestData {
    const char* envThreads;
    size_t hardwareThreads;
    size_t expectedThreads;
};

class WorkerClientThreadCalcTest : public ::testing::TestWithParam<WorkerThreadsTestData> {
};

TEST_P(WorkerClientThreadCalcTest, CalculatesOptimalThreadCount) {
    const char* envThreads = GetParam().envThreads;
    size_t inputHardware = GetParam().hardwareThreads;
    size_t expected = GetParam().expectedThreads;

    size_t actual = WorkerClient::CalculateThreadCount(envThreads, inputHardware);

    EXPECT_EQ(expected, actual);
}

// LOGGRID_WORKER_THREADS not set: N-1 rule on the hardware thread count
INSTANTIATE_TEST_SUITE_P(
    HardwareEdgeCases,
    WorkerClientThreadCalcTest,
    ::testing::Values(
        WorkerThreadsTestData{nullptr, 0, 4},
        WorkerThreadsTestData{nullptr, 1, 1},
        WorkerThreadsTestData{nullptr, 2, 1},
        WorkerThreadsTestData{nullptr, 8, 7},
        WorkerThreadsTestData{nullptr, 32, 31}
    )
);

// Empty value and "0" mean auto, same as not set
INSTANTIATE_TEST_SUITE_P(
    EnvAuto,
    WorkerClientThreadCalcTest,
    ::testing::Values(
        WorkerThreadsTestData{"", 8, 7},
        WorkerThreadsTestData{"0", 8, 7},
        WorkerThreadsTestData{"0", 1, 1},
        WorkerThreadsTestData{"00", 8, 7}
    )
);

// Valid value is used as is: no N-1 rule, independent of the hardware
INSTANTIATE_TEST_SUITE_P(
    EnvExplicit,
    WorkerClientThreadCalcTest,
    ::testing::Values(
        WorkerThreadsTestData{"1", 8, 1},
        WorkerThreadsTestData{"2", 8, 2},
        WorkerThreadsTestData{"8", 8, 8},
        WorkerThreadsTestData{"16", 8, 16},
        WorkerThreadsTestData{"2", 0, 2},
        WorkerThreadsTestData{"256", 8, WorkerClient::kMaxThreads}
    )
);

// Invalid value falls back to the N-1 rule
INSTANTIATE_TEST_SUITE_P(
    EnvInvalid,
    WorkerClientThreadCalcTest,
    ::testing::Values(
        WorkerThreadsTestData{"abc", 8, 7},
        WorkerThreadsTestData{"4x", 8, 7},
        WorkerThreadsTestData{"-1", 8, 7},
        WorkerThreadsTestData{"+4", 8, 7},
        WorkerThreadsTestData{" 4", 8, 7},
        WorkerThreadsTestData{"4 ", 8, 7},
        WorkerThreadsTestData{"2.5", 8, 7},
        WorkerThreadsTestData{"257", 8, 7},
        WorkerThreadsTestData{"99999999999999999999999", 8, 7}
    )
);
