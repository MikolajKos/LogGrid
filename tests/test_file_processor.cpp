#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "FileProcessor.hpp"

TEST(FileProcessorTest, HandlesMissingFileGracefully) {
    LogSystem::TaskPayload dummyTask;

    std::string dummyFilePath = "absolutely/random/filepath/good_luck_finding_it.txt";
    std::string dummyKeyword = "super mega dangerous error";

    strncpy(dummyTask.filename, dummyFilePath.c_str(), sizeof(dummyTask.filename));
    dummyTask.filename[sizeof(dummyTask.filename) - 1] = '\0';
    strncpy(dummyTask.keyword, dummyKeyword.c_str(), sizeof(dummyTask.keyword));
    dummyTask.keyword[sizeof(dummyTask.keyword) - 1] = '\0';

    dummyTask.start_offset = 0;
    dummyTask.end_offset = 1000;

    dummyTask.search_id = 1;
    dummyTask.task_id = 1;

    bool lineFoundCalled = false;
    bool taskDoneCalled = false;    

    auto onLineFound = [&](const std::string& line) {
        lineFoundCalled = true;
    };

    // onTaskDone is called when file was not found
    auto onTaskDone = [&]() {
        taskDoneCalled = true;
    };

    FileProcessor::SearchTask(dummyTask, onLineFound, onTaskDone);

    EXPECT_FALSE(lineFoundCalled);
    EXPECT_TRUE(taskDoneCalled);
}