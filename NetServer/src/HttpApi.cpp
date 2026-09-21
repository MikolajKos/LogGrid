#include "HttpApi.hpp"

void HttpApi::RegisterRoutes(httplib::Server& svr) {
    svr.Post("/api/search", [this](const httplib::Request& req, httplib::Response& res) {
        m_controller.HandlePostSearch(req, res);
    });

    svr.Get("/api/status/:id", [this](const httplib::Request& req, httplib::Response& res) {
        m_controller.HandleGetSearch(req, res);
    });
}

void HttpApi::Start(uint16_t port) {
    RegisterRoutes(m_server);
    m_thread = std::jthread([this, port]() {
        m_server.listen("0.0.0.0", port);
    });
}

void HttpApi::Stop() {
    m_server.stop();
}