#ifndef FILE_PROCESSOR_HPP
#define FILE_PROCESSOR_HPP

#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <string>

#include "LogSearchCommon.hpp"

class FileProcessor {
public:
    static void SearchTask(
        const LogSystem::TaskPayload& task,
        std::function<void(const std::string&)> onLineFound,
        std::function<void()> onTaskDone
    ) {
        std::ifstream file(task.filename, std::ios::binary);

        if (!file.is_open()) {
            std::cout << "[WORKER] Could not open file: " << task.filename << "\n";
            return;
        }
        
        uint64_t startByte = task.start_line;
        uint64_t endByte = task.end_line;

        file.seekg(startByte);

        // It means we are somewhere in the middle of a text
        if (startByte != 0) {
            // If prev char is '\n' it means that we start perfectly and we dont skip line
            file.seekg(startByte - 1);
            char prevChar;
            file.get(prevChar);

            if (prevChar != '\n') {
                std::string dummy;
                std::getline(file, dummy);
            }
        }

        // Now we can read
        std::string line;

        uint64_t currentPos = static_cast<uint64_t>(file.tellg());

        std::regex pattern(task.keyword);
        
        while (currentPos <= endByte && std::getline(file, line)) {
            // For Windows format (CRLF) delete '\r'
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (std::regex_search(line, pattern)) {
                onLineFound(line);
            }

            // Position update
            std::streampos pos = file.tellg();

            // On EOF getline returns -1, but negative number cannot be store within uint64_t
            if (pos == std::streampos(-1)) {
                break;
            }

            currentPos = static_cast<uint64_t>(pos);
        }

        onTaskDone();
    }
};

#endif // FILE_PROCESSOR_HPP