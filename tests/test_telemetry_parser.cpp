#include "TelemetryParser.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <optional>

static constexpr std::string_view kSampleLine = "1,946684823847,channel_60,0.7188446";

TEST(TelemetryParserTest, ParsesValidValues) {
    EXPECT_EQ(TelemetryParser::ParseScaledValue("0.7188446", 7), 7188446);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("-112.23", 7), -1122300000);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("5", 7), 50000000);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("0", 7), 0);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("12", 0), 12);
}

TEST(TelemetryParserTest, RejectsTooManyFractionDigits) {
    EXPECT_EQ(TelemetryParser::ParseScaledValue("0.7188446", 6), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("0.12345678", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("1.5", 0), std::nullopt);
}

TEST(TelemetryParserTest, RejectsOverflow) {
    EXPECT_EQ(TelemetryParser::ParseScaledValue("922337203685.4775807", 7), INT64_MAX);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("-922337203685.4775807", 7), -INT64_MAX);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("922337203685.4775808", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("922337203686", 7), std::nullopt);
    // does not fit in uint64_t before scaling
    EXPECT_EQ(TelemetryParser::ParseScaledValue("18446744073709551616", 0), std::nullopt);
}

TEST(TelemetryParserTest, RejectsInvalidFormat) {
    EXPECT_EQ(TelemetryParser::ParseScaledValue("", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("-", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("5.", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue(".5", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("5.ab", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("--5", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("+5", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("5.-1", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("1.2.3", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("1e3", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue(" 5", 7), std::nullopt);
}

TEST(TelemetryParserTest, RejectsInvalidScaleDigits) {
    EXPECT_EQ(TelemetryParser::ParseScaledValue("5", -1), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("5", 19), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseScaledValue("5", 18), 5'000'000'000'000'000'000);
}

TEST(TelemetryParserTest, GetFieldReturnsRequestedField) {
    EXPECT_EQ(TelemetryParser::GetField("a,b,c,d", 0, 4), "a");
    EXPECT_EQ(TelemetryParser::GetField("a,b,c,d", 3, 4), "d");
    EXPECT_EQ(TelemetryParser::GetField("a,,c,d", 1, 4), "");
}

TEST(TelemetryParserTest, GetFieldRejectsWrongFieldCount) {
    EXPECT_EQ(TelemetryParser::GetField("a,b,c", 0, 4), std::nullopt);
    EXPECT_EQ(TelemetryParser::GetField("a,b,c,d,e", 0, 4), std::nullopt);
    EXPECT_EQ(TelemetryParser::GetField("a,b,c,d", 4, 4), std::nullopt);
}

TEST(TelemetryParserTest, ParseLineReadsValueColumn) {
    EXPECT_EQ(TelemetryParser::ParseLine(kSampleLine, 7), 7188446);
    EXPECT_EQ(TelemetryParser::ParseLine("1,2000-01-01T00:00:00,channel_1", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseLine("1,2000-01-01T00:00:00,channel_1,0.5,x", 7), std::nullopt);
    EXPECT_EQ(TelemetryParser::ParseLine("1,2000-01-01T00:00:00,channel_1,abc", 7), std::nullopt);
}
