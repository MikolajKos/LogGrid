#include "SearchController.hpp"

using Status = httplib::StatusCode;
using namespace std::chrono_literals;

void SearchController::HandlePostSearch(const httplib::Request& req, httplib::Response& res) {
    SearchConfig config;
    
    try {
        auto body = nlohmann::json::parse(req.body);

        // Populate config
        config.max_results = body.value("maxResults", config.max_results); // Has default value
        config.output_dir = body.value("outputDir", config.output_dir);

        std::string path = body.at("path").get<std::string>();
        std::string keyword = body.at("keyword").get<std::string>();

        uint64_t searchId = m_service.StartSearch(path, keyword, config);

        nlohmann::json response = { {"searchId", searchId} };
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
            SearchStatus session = *optStatus;
            
            nlohmann::json response = {
                { "searchId", sessionId },
                { "state", session.state == SearchState::Running ? "Running" : "Done" },
                { "chunksDone", session.chunks_done },
                { "chunksTotal", session.chunks_total },
                { "linesCount", session.lines_found },
                { "totalMatches", session.total_matches }
            };

            res.set_content(response.dump(), "application/json");
            res.status = Status::OK_200;
        }
        else { // Session Not Found
            throw std::runtime_error("Session Not Found");
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