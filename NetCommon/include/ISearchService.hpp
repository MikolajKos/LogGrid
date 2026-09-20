#ifndef I_SEARCH_SERVICE_HPP
#define I_SEARCH_SERVICE_HPP

#include "LogSearchCommon.hpp"

#include <future>
#include <string>

class ISearchService {
public:
    virtual std::future<LogSystem::SearchResult> StartSearch(const std::string& path, const std::string& keyword) = 0;
};

#endif // I_SEARCH_SERVICE_HPP