#ifndef I_SEARCH_SERVICE_HPP
#define I_SEARCH_SERVICE_HPP

#include "LogSearchCommon.hpp"

#include <future>
#include <optional>
#include <string>

enum class SearchState { Running, Done };

struct SearchStatus {
    SearchState state;
    uint64_t lines_found = 0;
    uint64_t total_matches;
    int chunks_done = 0;
    int chunks_total = 0;
};

struct SearchConfig {
    uint64_t max_results = 10000; // line count
};

class ISearchService {
public:
    virtual uint64_t StartSearch(const std::string& path, const std::string& keyword, const SearchConfig& config) = 0;
    virtual std::optional<SearchStatus> GetStatus(const uint64_t search_id) = 0;
    
    virtual ~ISearchService() = default;
};

#endif // I_SEARCH_SERVICE_HPP