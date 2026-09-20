#ifndef SEARCH_CONTROLLER_HPP
#define SEARCH_CONTROLLER_HPP

#include "ISearchService.hpp"
#include "LogSearchCommon.hpp"

#include "httplib.h"
#include "json.hpp"

#include <cstdlib>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

class SearchController {
public:
    SearchController(ISearchService& svc): m_service(svc) {};

    void HandlePostSearch(const httplib::Request& req, httplib::Response& res);
    void HandleGetSearch(const httplib::Request& req, httplib::Response& res);
private:
    void RegisterSession(SearchHandle& handle);

private:
    ISearchService& m_service;

    std::unordered_map<uint64_t, std::future<LogSystem::SearchResult>> m_sessions;
    std::mutex m_mutex;
};

#endif // SEARCH_CONTROLLER_HPP