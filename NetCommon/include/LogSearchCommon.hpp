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
        Worker_TaskDone,        // Worker finished analysing chunk, returns result batch
        Server_JobFinished      // All work done
    };

    struct TaskPayload {
        uint64_t search_id;
        uint64_t task_id;
        uint64_t max_results;
        uint64_t start_offset;
        uint64_t end_offset;
        char keyword[64];       // Search criteria
        char filename[256];
    };
    
    struct ChunkResult {
        std::vector<std::string> lines;
        uint64_t search_id{0};
        uint64_t task_id{0};
        uint64_t total_matches{0};
        uint32_t lines_found{0};
    };
    
    struct HelloMessage {
        uint64_t threads_available;
    };
}

#endif // LOG_SEARCH_COMMON_HPP
