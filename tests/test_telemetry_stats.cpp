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

TEST(TelemetryStatsTest, AddErrorCountsOnlyErrors) {
    LogSystem::TelemetryStats stats{};
    stats.AddError();
    stats.AddError();

    EXPECT_EQ(stats.errors, 2u);
    EXPECT_EQ(stats.count, 0u);
    EXPECT_EQ(stats.sum, 0);
}

TEST(TelemetryStatsTest, AddSetsOverflowAbove) {
    LogSystem::TelemetryStats stats{};
    stats.sum = INT64_MAX;
    stats.Add(1);

    EXPECT_EQ(stats.count, 1u);
    EXPECT_TRUE(stats.overflow);
}

TEST(TelemetryStatsTest, AddSetsOverflowBelow) {
    LogSystem::TelemetryStats stats{};
    stats.sum = INT64_MIN;
    stats.Add(-1);

    EXPECT_EQ(stats.count, 1u);
    EXPECT_TRUE(stats.overflow);
}

TEST(TelemetryStatsTest, MergeCombinesTwoResults) {
    LogSystem::TelemetryStats statsA;
    statsA.Add(10);
    statsA.Add(20);

    LogSystem::TelemetryStats statsB;
    statsB.Add(5);
    statsB.AddError();

    statsA.Merge(statsB);

    EXPECT_EQ(statsA.count, 3u);
    EXPECT_EQ(statsA.sum, 35);
    EXPECT_EQ(statsA.errors, 1u);
    EXPECT_EQ(statsA.min, 5);
    EXPECT_EQ(statsA.max, 20);
}
TEST(TelemetryStatsTest, MergeWithEmptyKeepsValues) {
    LogSystem::TelemetryStats statsA;
    statsA.Add(10);

    LogSystem::TelemetryStats statsB;   // empty: count == 0, min/max at initial values

    statsA.Merge(statsB);

    EXPECT_EQ(statsA.count, 1u);
    EXPECT_EQ(statsA.errors, 0u);
    EXPECT_EQ(statsA.sum, 10);
    EXPECT_EQ(statsA.min, 10);
    EXPECT_EQ(statsA.max, 10);
    EXPECT_FALSE(statsA.overflow);
}

TEST(TelemetryStatsTest, MergeOfEmptyKeepsInitialMinMax) {
    LogSystem::TelemetryStats statsA;
    LogSystem::TelemetryStats statsB;

    statsA.Merge(statsB);

    EXPECT_EQ(statsA.count, 0u);
    EXPECT_EQ(statsA.sum, 0);
    EXPECT_EQ(statsA.min, INT64_MAX);
    EXPECT_EQ(statsA.max, INT64_MIN);
    EXPECT_FALSE(statsA.overflow);
}

TEST(TelemetryStatsTest, MergeCarriesOverflow) {
    LogSystem::TelemetryStats statsA;
    statsA.Add(10);

    LogSystem::TelemetryStats statsB;
    statsB.overflow = true;             // sum of B already overflowed in its chunk

    statsA.Merge(statsB);

    EXPECT_TRUE(statsA.overflow);
}

TEST(TelemetryStatsTest, MergeSetsOverflowOnSum) {
    // Neither result has overflowed on its own, but their sum does not fit in int64_t
    LogSystem::TelemetryStats statsA;
    statsA.sum = INT64_MAX;

    LogSystem::TelemetryStats statsB;
    statsB.sum = 1;

    statsA.Merge(statsB);

    EXPECT_TRUE(statsA.overflow);
}
