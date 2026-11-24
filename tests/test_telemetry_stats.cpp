#include "TelemetryStats.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

struct AddTestCase {
    std::string name;
    std::vector<int64_t> values;
    int64_t expectedSum;
    int64_t expectedMin;
    int64_t expectedMax;
};

class TelemetryStatsAddTest : public ::testing::TestWithParam<AddTestCase> {};

TEST_P(TelemetryStatsAddTest, AddUpdatesAllFields) {
    LogSystem::TelemetryStats stats;
    for (int64_t value : GetParam().values) {
        stats.Add(value);
    }

    EXPECT_EQ(stats.count, GetParam().values.size());
    EXPECT_EQ(stats.min, GetParam().expectedMin);
    EXPECT_EQ(stats.max, GetParam().expectedMax);
    EXPECT_EQ(stats.sum, GetParam().expectedSum);
    EXPECT_EQ(stats.errors, 0u);
    EXPECT_FALSE(stats.overflow);
}

INSTANTIATE_TEST_SUITE_P(
    AddCases,                       // prefix in test names
    TelemetryStatsAddTest,          // fixture class from TEST_P
    ::testing::Values(
        AddTestCase{ "SingleValue", { 5 },          5,  5,  5 },
        AddTestCase{ "Increasing",  { 1, 2, 3 },    6,  1,  3 },
        AddTestCase{ "Decreasing",  { 3, 2, 1 },    6,  1,  3 },
        AddTestCase{ "MixedSigns",  { -10, 0, 7 }, -3, -10, 7 },
        AddTestCase{ "SameValue",   { 4, 4, 4 },   12,  4,  4 }
    ),
    // Use the name field as the test name in the output
    [](const ::testing::TestParamInfo<AddTestCase>& info) {
        return info.param.name;
    }
);