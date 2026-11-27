#ifndef LOG_SEARCH_COMMON_HPP
#define LOG_SEARCH_COMMON_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "TelemetryStats.hpp"

#include "olc_net.hpp"

namespace LogSystem {
    inline constexpr int kValueScaleDigits = 10;   // max fraction digits of float32 values in [0.01, 1)
    
    consteval int64_t ComputeValueScale() {
        static_assert(kValueScaleDigits >= 0 && kValueScaleDigits <= 18);
        
        int64_t result = 1;
        for (int i = 0; i < kValueScaleDigits; ++i) {
            result *= 10;
        }
        return result;
    }
    
    inline constexpr int64_t kValueScale = ComputeValueScale();

    enum class LogSearchMsg {
        Worker_Hello,           // Worker is ready
        Server_SearchTask,      // Master sends TaskPayload structure
        Worker_TaskDone,        // Worker finished analysing chunk, returns result batch
        Server_JobFinished      // All work done
    };

    struct TaskPayload {
        uint64_t search_id;
        uint64_t task_id;
        uint64_t start_offset;
        uint64_t end_offset;
        char filename[256];
    };
    
    struct ChunkResult {
        uint64_t search_id{0};
        uint64_t task_id{0};
        
        TelemetryStats stats{};
    };
    
    struct HelloMessage {
        uint64_t threads_available;
    };
}

#endif // LOG_SEARCH_COMMON_HPP
