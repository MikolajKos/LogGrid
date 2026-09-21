#include "SearchController.hpp"

using Status = httplib::StatusCode;
using namespace std::chrono_literals;

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

        nlohmann::json response = { {"searchId", handle.search_id} };
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
    uint64_t sessionId;

    try {
        sessionId = std::stoull(req.path_params.at("id"));

        auto optStatus = m_service.GetStatus(sessionId);

        // Session running
        if (optStatus) {
            SearchStatus sessionStatus = *optStatus;

            // enum doesn't serialize to json as string
            std::string state = sessionStatus.state == SearchState::Running ? "Running" : "Done";
            
            nlohmann::json response = {
                { "searchId", sessionId },
                { "state", state},
                { "chunksDone", sessionStatus.chunks_done},
                { "chunksTotal", sessionStatus.chunks_total},
                { "linesFound", sessionStatus.lines_found}
            };

            res.set_content(response.dump(), "application/json");
            res.status = Status::OK_200;
        }
        else { // Session Done or Not Found
            std::shared_future<LogSystem::SearchResult> sharedFuture;
            
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_sessions.find(sessionId);
                
                if (it == m_sessions.end()) {
                    throw std::runtime_error("Session not found");
                }
                
                sharedFuture = it->second;
            }

            // Session done but promise not set
            if (sharedFuture.wait_for(0s) != std::future_status::ready) {
                res.set_content("Please try again", "text/plain");
                res.status = Status::Accepted_202;
                return;
            }

            LogSystem::SearchResult result = sharedFuture.get();

            nlohmann::json response = {
                { "searchId", sessionId },
                { "state", "Done" },
                { "linesFound", result.lines.size() }
            };

            res.set_content(response.dump(), "application/json");
            res.status = Status::OK_200;
        }

    }
    catch (const nlohmann::json::exception& e) {
        res.set_content("Bad Request", "text/plain");
        res.status = Status::BadRequest_400;
    }
    catch (const std::runtime_error& e) {
        res.set_content(e.what(), "text/plain");
        res.status = Status::NotFound_404;
    }
    catch (const std::exception& e) {
        res.set_content("Bad Request", "text/plain");
        res.status = Status::BadRequest_400;
    }
}

void SearchController::RegisterSession(SearchHandle& handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(handle.search_id);
    
    if (it != m_sessions.end()) {
        throw std::runtime_error(std::format("ID: {} was already in use by other session.", handle.search_id));        
    }

    m_sessions[handle.search_id] = handle.future.share();
}