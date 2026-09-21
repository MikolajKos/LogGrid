#ifndef HTTP_API_HPP
#define HTTP_API_HPP

#include "SearchController.hpp"

#include "httplib.h"

#include <thread>

class HttpApi {
public:
    HttpApi(ISearchService& service): m_controller(service) {};
    void RegisterRoutes(httplib::Server& svr);
    void Start(uint16_t port);
    void Stop();
private:
    std::jthread m_thread;
    httplib::Server m_server;
    SearchController m_controller;
};

#endif // HTTP_API_HPP