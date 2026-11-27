#ifndef FILE_PROCESSOR_HPP
#define FILE_PROCESSOR_HPP

#include "LogSearchCommon.hpp"
#include "TelemetryParser.hpp"
#include "TelemetryStats.hpp"

#include <fstream>
#include <iostream>
#include <string>

// The parser's power table must give the same scale as the rest of the system
static_assert(TelemetryParser::kPow10[LogSystem::kValueScaleDigits] == LogSystem::kValueScale,
              "TelemetryParser::kPow10 does not match LogSystem::kValueScale");

class FileProcessor {
public:
    static LogSystem::ChunkResult ProcessChunk(const LogSystem::TaskPayload& task) {
        std::ifstream file(task.filename, std::ios::binary);

        if (!file.is_open()) {
            std::cout << "[WORKER] Could not open file: " << task.filename << "\n";
            LogSystem::ChunkResult result;
            result.stats.AddFailedChunk();
            
            return result;
        }
        
        const uint64_t startByte = task.start_offset;
        const uint64_t endByte = task.end_offset;
        
        file.seekg(startByte);
        
        // It means we are somewhere in the middle of a text
        if (startByte != 0) {
            // If prev char is '\n' it means that we start perfectly and we dont skip line
            file.seekg(startByte - 1);
            char prevChar;
            file.get(prevChar);

            if (prevChar != '\n') {
                std::string skipped;
                std::getline(file, skipped);
                // One line was skipped because it was read in previous chubk
            }
        }
        
        LogSystem::ChunkResult batch;
        std::string line;
        uint64_t currentPos = static_cast<uint64_t>(file.tellg());
        
        while (currentPos < endByte && std::getline(file, line)) {
            // For Windows format (CRLF) delete '\r'
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            auto value = TelemetryParser::ParseLine(line, LogSystem::kValueScaleDigits);

            // Check and update statistics
            value ? batch.stats.Add(*value) : batch.stats.AddError();

            // Position update
            std::streampos pos = file.tellg();

            // On EOF getline returns -1, but negative number cannot be store within uint64_t
            if (pos == std::streampos(-1)) {
                break;
            }

            currentPos = static_cast<uint64_t>(pos);
        }

        return batch;
    }
};

#endif // FILE_PROCESSOR_HPP