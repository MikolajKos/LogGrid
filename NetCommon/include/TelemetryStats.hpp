#ifndef TELEMETRY_STATS_HPP
#define TELEMETRY_STATS_HPP

#include <algorithm>
#include <cstdint>

namespace LogSystem {
    struct TelemetryStats {
        uint64_t count{0};
        uint64_t errors{0};
        uint64_t failed_chunks{0};
        int64_t  sum{0};
        int64_t  min{INT64_MAX};
        int64_t  max{INT64_MIN};
        bool     overflow{false};
    
        void Add(const int64_t value) {
            min = std::min(min, value);
            max = std::max(max, value);
            
            if (__builtin_add_overflow(sum, value, &sum)) {
                overflow = true;
            }
            ++count;
        }
    
        void AddFailedChunk() {  ++failed_chunks; }

        void AddError() { ++errors; }
        
        void Merge(const TelemetryStats& other) {
            if (other.overflow) overflow = true;
            
            if (__builtin_add_overflow(sum, other.sum, &sum)) {
                overflow = true;
            }
    
            count += other.count;
            min = std::min(min, other.min);
            max = std::max(max, other.max);
            errors += other.errors;
            failed_chunks += other.failed_chunks;
        }
    };
}

#endif // TELEMETRY_STATS_HPP