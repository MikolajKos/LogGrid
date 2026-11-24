#ifndef I_SEARCH_SERVICE_HPP
#define I_SEARCH_SERVICE_HPP

#include "LogSearchCommon.hpp"
#include "TelemetryStats.hpp"

#include <future>
#include <optional>
#include <string>

enum class SearchState { Running, Done };

struct SearchStatus {
    LogSystem::TelemetryStats stats{};
    SearchState state;
    uint64_t scan_errors{0};
    int chunks_done{0};
    int chunks_total{0};
};

struct SearchConfig {
    std::string output_dir = "/sessions/"; // default output catalog
};

class ISearchService {
public:
    virtual uint64_t StartSearch(const std::string& path, const SearchConfig& config) = 0;
    virtual std::optional<SearchStatus> GetStatus(const uint64_t search_id) = 0;
    
    virtual ~ISearchService() = default;
};

#endif // I_SEARCH_SERVICE_HPP