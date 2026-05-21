#ifndef LOG_SEARCH_COMMON_HPP
#define LOG_SEARCH_COMMON_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "olc_net.hpp"

namespace LogSystem {
    enum class LogSearchMsg {
        Worker_Hello,           // Worker is ready
        Server_SearchTask,      // Master sends TaskPayload structure
        Worker_FoundLine,       // Worker sends matching code line
        Worker_TaskDone,        // Worker finished analysing chunk, asking for more
        Server_JobFinished      // All work done
    };

    // I chose 64 unsigned bit number over 32, 
    // because 32 bit number alows to read maximum of 4GB file
    struct TaskPayload {
        char keyword[64];       // Search criteria
        char filename[128];
        uint64_t start_line;    // Line offset
        uint64_t end_line;      // End of chunk (0 means search till EOF)
    };
}

#endif // LOG_SEARCH_COMMON_HPP