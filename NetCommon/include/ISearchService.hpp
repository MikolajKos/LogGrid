#ifndef I_SEARCH_SERVICE_HPP
#define I_SEARCH_SERVICE_HPP

#include "LogSearchCommon.hpp"

#include <future>
#include <string>

struct SearchConfig {
    uint64_t max_results = 10000; // line count
};

struct SearchHandle {
    uint64_t search_id;
    std::future<LogSystem::SearchResult> future;
};

class ISearchService {
public:
    virtual SearchHandle StartSearch(const std::string& path, const std::string& keyword, const SearchConfig& config) = 0;
    virtual ~ISearchService() = default;
};

#endif // I_SEARCH_SERVICE_HPP