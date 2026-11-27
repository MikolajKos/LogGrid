#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "FileProcessor.hpp"

namespace {
    // Expected values below are written for this scale
    static_assert(LogSystem::kValueScaleDigits == 10, "update the expected scaled values");

    constexpr int64_t kTenth = 1'000'000'000;   // 0.1 scaled by 10^10

    // 10-byte telemetry lines: "id,timestamp,channel,value\n"
    const std::string kLine1 = "1,0,c,0.1\n";   // bytes 0..9
    const std::string kLine2 = "2,0,c,0.2\n";   // bytes 10..19
    const std::string kLine3 = "3,0,c,0.3\n";   // bytes 20..29
    const std::string kThreeLines = kLine1 + kLine2 + kLine3;

    const std::string kTestFile = "test_file.log";

    LogSystem::TaskPayload MakeTask(const std::string& filename, uint64_t start, uint64_t end) {
        LogSystem::TaskPayload task{};
        std::strncpy(task.filename, filename.c_str(), sizeof(task.filename) - 1);
        task.start_offset = start;
        task.end_offset = end;
        task.search_id = 1;
        task.task_id = 1;
        return task;
    }

    void WriteTestFile(const std::string& content) {
        std::ofstream file(kTestFile, std::ios::binary);
        file << content;
    }
}

TEST(FileProcessorTest, MissingFileGivesEmptyResult) {
    auto result = FileProcessor::ProcessChunk(
        MakeTask("absolutely/random/filepath/good_luck_finding_it.txt", 0, 1000));

    EXPECT_EQ(result.stats.failed_chunks, 1u);
    EXPECT_EQ(result.stats.count, 0u);
    EXPECT_EQ(result.stats.errors, 0u);
}

TEST(FileProcessorTest, StandardFileStats) {
    WriteTestFile(kThreeLines);

    auto result = FileProcessor::ProcessChunk(MakeTask(kTestFile, 0, 1000));

    EXPECT_EQ(result.stats.count, 3u);
    EXPECT_EQ(result.stats.errors, 0u);
    EXPECT_EQ(result.stats.sum, 6 * kTenth);
    EXPECT_EQ(result.stats.min, 1 * kTenth);
    EXPECT_EQ(result.stats.max, 3 * kTenth);
    EXPECT_FALSE(result.stats.overflow);

    std::filesystem::remove(kTestFile);
}

// Splitting the file at any byte must count every line in exactly one of the two chunks
TEST(FileProcessorTest, EveryLineCountedOnceForAnySplit) {
    WriteTestFile(kThreeLines);
    const uint64_t size = kThreeLines.size();

    for (uint64_t split = 1; split < size; ++split) {
        auto first = FileProcessor::ProcessChunk(MakeTask(kTestFile, 0, split));
        auto second = FileProcessor::ProcessChunk(MakeTask(kTestFile, split, size));

        EXPECT_EQ(first.stats.count + second.stats.count, 3u) << "split at byte " << split;
        EXPECT_EQ(first.stats.sum + second.stats.sum, 6 * kTenth) << "split at byte " << split;
        EXPECT_EQ(first.stats.errors + second.stats.errors, 0u) << "split at byte " << split;
    }

    std::filesystem::remove(kTestFile);
}

// =====================================================================

struct FileTestCase {
    std::string testName;
    std::string fileContent;
    uint64_t startOffset;
    uint64_t endOffset;
    uint64_t expectedCount;
    uint64_t expectedErrors;
    int64_t expectedSum;
};

class FileProcessorParamTest : public ::testing::TestWithParam<FileTestCase> {
protected:
    void SetUp() override {
        WriteTestFile(GetParam().fileContent);
    }

    void TearDown() override {
        std::filesystem::remove(kTestFile);
    }
};

TEST_P(FileProcessorParamTest, ComputesStatsForChunk) {
    auto result = FileProcessor::ProcessChunk(
        MakeTask(kTestFile, GetParam().startOffset, GetParam().endOffset));

    EXPECT_EQ(result.stats.count, GetParam().expectedCount);
    EXPECT_EQ(result.stats.errors, GetParam().expectedErrors);
    EXPECT_EQ(result.stats.sum, GetParam().expectedSum);
}

INSTANTIATE_TEST_SUITE_P(
    FileChunkingEdgeCases,
    FileProcessorParamTest,
    ::testing::Values(
        // Start offset right after '\n': the chunk starts with a whole line
        FileTestCase{ "PerfectStartOffset", kLine1 + kLine2, 10, 1000, 1, 0, 2 * kTenth },

        // Start offset inside a line: that line belongs to the previous chunk and is skipped
        FileTestCase{ "StartOffsetInsideLine", kThreeLines, 11, 1000, 1, 0, 3 * kTenth },

        // End offset inside a line: the line that starts before it is read to the end
        FileTestCase{ "EndOffsetInTheMiddleOfLine", kThreeLines, 0, 12, 2, 0, 3 * kTenth },

        // A line starting exactly at end_offset belongs to the next chunk
        FileTestCase{ "LineAtBoundaryNotReadByPreviousChunk", kThreeLines, 0, 10, 1, 0, 1 * kTenth },
        FileTestCase{ "LineAtBoundaryReadByNextChunk", kThreeLines, 10, 30, 2, 0, 5 * kTenth },

        // '\r' of CRLF line endings is removed before parsing
        FileTestCase{ "WindowsCRLFHandling", "1,0,c,0.1\r\n2,0,c,0.2\r\n", 0, 1000, 2, 0, 3 * kTenth },

        // The last line without '\n' is still read
        FileTestCase{ "NoTrailingNewline", kLine1 + "2,0,c,0.2", 0, 1000, 2, 0, 3 * kTenth },

        // Lines the parser rejects are counted in errors, including an empty line
        FileTestCase{ "InvalidLinesCountedAsErrors",
                      kLine1 + "garbage\n" + "4,0,c,abc\n" + "\n" + kLine2, 0, 1000, 2, 3, 3 * kTenth },

        FileTestCase{ "EmptyFile", "", 0, 1000, 0, 0, 0 },

        FileTestCase{ "OffsetOutOfBounds", kLine1, 5000, 6000, 0, 0, 0 }
    ),
    // Format test names for the console output
    [](const ::testing::TestParamInfo<FileTestCase>& info) {
        return info.param.testName;
    }
);
