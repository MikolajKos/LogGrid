#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "FileProcessor.hpp"

TEST(FileProcessorTest, HandlesMissingFileGracefully) {
    LogSystem::TaskPayload dummyTask = {}; 

    std::string dummyFilePath = "absolutely/random/filepath/good_luck_finding_it.txt";
    std::string dummyKeyword = "super mega dangerous error";

    strncpy(dummyTask.filename, dummyFilePath.c_str(), sizeof(dummyTask.filename) - 1);
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    
    strncpy(dummyTask.keyword, dummyKeyword.c_str(), sizeof(dummyTask.keyword) - 1);
    dummyTask.keyword[sizeof(dummyTask.keyword) - 1] = '\0';

    dummyTask.start_offset = 0;
    dummyTask.end_offset = 1000;
    dummyTask.search_id = 1;
    dummyTask.task_id = 1;
    dummyTask.max_results = 10000; 

    auto result = FileProcessor::SearchTask(dummyTask, dummyTask.max_results);

    EXPECT_TRUE(result.lines.empty());
    EXPECT_EQ(result.lines_found, 0);
    EXPECT_EQ(result.total_matches, 0);
}


// =====================================================================

struct FileTestCase {
    std::string testName;
    std::string fileContent;
    uint64_t startOffset;
    uint64_t endOffset;
    std::string keyword;
    int expectedMatchCount;
};

class FileProcessorParamTest : public ::testing::TestWithParam<FileTestCase> {
protected:
    void SetUp() override {
        std::ofstream file;
        file.open("test_file.log");
        
        file << GetParam().fileContent;

        file.close();
    }

    void TearDown() override {
        std::filesystem::remove("test_file.log");
    }
};

TEST_P(FileProcessorParamTest, FindLinesInChunksTest) {
    // Create Search Task
    LogSystem::TaskPayload dummyTask = {};
    
    dummyTask.start_offset = GetParam().startOffset;
    dummyTask.end_offset = GetParam().endOffset;

    std::string filename = "test_file.log";
    std::string keyword = GetParam().keyword;

    strncpy(dummyTask.filename, filename.c_str(), sizeof(dummyTask.filename) - 1);
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    
    strncpy(dummyTask.keyword, keyword.c_str(), sizeof(dummyTask.keyword) - 1);
    dummyTask.keyword[sizeof(dummyTask.keyword) - 1] = '\0';

    dummyTask.search_id = 1;
    dummyTask.task_id = 1;
    dummyTask.max_results = 10000;


    auto result = FileProcessor::SearchTask(dummyTask, dummyTask.max_results);

    EXPECT_EQ(result.total_matches, GetParam().expectedMatchCount);
    EXPECT_EQ(result.lines_found, GetParam().expectedMatchCount);
    EXPECT_EQ(result.lines.size(), GetParam().expectedMatchCount);
}


INSTANTIATE_TEST_SUITE_P(
    FileChunkingEdgeCases,
    FileProcessorParamTest,
    ::testing::Values(
        // 1. Standard file
        FileTestCase{
            "StandardFile", 
            "INFO start\nERROR first\nINFO middle\nERROR second\n", 
            0, 1000, "ERROR", 2
        },

        // 2. Start offset hits exactly after \n
        FileTestCase{
            "PerfectStartOffset", 
            "LINE 1\nERROR should be found\n", 
            7, 1000, "ERROR", 1
        },

        // 3. Start offset hits inside the first word
        FileTestCase{
            "CutFirstLetterOfKeyword", 
            "LINE 1\nERROR should be skipped\nERROR but this stays\n", 
            8, 1000, "ERROR", 1
        },

        // 4. End offset ends mid-line, std::getline should read until \n anyway
        FileTestCase{
            "EndOffsetInTheMiddleOfLine", 
            "ERROR 1\nERROR 2\nINFO 3\n", 
            0, 12, "ERROR", 2
        },

        // 5. CRLF (\r\n) handling check
        FileTestCase{
            "WindowsCRLFHandling", 
            "INFO\r\nERROR\r\nINFO\r\n", 
            0, 1000, "ERROR$", 1
        },

        // 6. No \n at EOF
        FileTestCase{
            "NoTrailingNewline", 
            "INFO 1\nINFO 2\nERROR at the very end", 
            0, 1000, "ERROR", 1
        },

        // 7. Empty file check
        FileTestCase{
            "EmptyFile", 
            "", 
            0, 1000, "ERROR", 0
        },

        // 8. Offset way beyond file size
        FileTestCase{
            "OffsetOutOfBounds", 
            "ERROR line\n", 
            5000, 6000, "ERROR", 0
        },

        // 9. Regex functionality check
        FileTestCase{
            "RegexAdvancedPattern", 
            "timestamp [ERR-123] db crash\ntimestamp [INFO-123] ok\ntimestamp [ERR-999] memory\n", 
            0, 1000, "\\[ERR-\\d+\\]", 2
        },

        // 10. Invalid regex check
        FileTestCase{
            "InvalidRegex",
            "", 0, 0,
            "[invalid",
            0
        }
    ),
    // Format test names for the console output
    [](const ::testing::TestParamInfo<FileTestCase>& info) {
        return info.param.testName;
    }
);