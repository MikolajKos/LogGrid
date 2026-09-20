#include "SearchController.hpp"

using Status = httplib::StatusCode;

void SearchController::HandlePostSearch(const httplib::Request& req, httplib::Response& res) {
    SearchConfig config;
    
    try {
        auto body = nlohmann::json::parse(req.body);

        // Populate config
        config.max_results = body.value("maxResults", config.max_results); // Has default value

        std::string path = body.at("path").get<std::string>();
        std::string keyword = body.at("keyword").get<std::string>();

        SearchHandle handle = m_service.StartSearch(path, keyword, config);

        RegisterSession(handle);

        nlohmann::json response = { {"search_id", handle.search_id} };
        res.set_content(response.dump(), "application/json");
        res.status = Status::Accepted_202;
    }
    catch (const nlohmann::json::exception& e) { // Bad Request 400
        res.set_content("Bad Request", "text/plain");
        res.status = Status::BadRequest_400;
        return;
    }
    catch (const std::runtime_error& e) {
        res.set_content(std::format("Runtime Error: {}", e.what()), "text/plain");
        res.status = Status::NotFound_404;
        return;
    }
    catch (const std::exception& e) { // Internal Error 500
        res.set_content("Internal Server Error", "text/plain");
        res.status = Status::InternalServerError_500;
        return;
    }
}

void SearchController::HandleGetSearch(const httplib::Request& req, httplib::Response& res) {

}

void SearchController::RegisterSession(SearchHandle& handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(handle.search_id);
    
    if (it != m_sessions.end()) {
        throw std::runtime_error(std::format("ID: {} was already in use by other session.", handle.search_id));        
    }

    m_sessions[handle.search_id] = std::move(handle.future);
}