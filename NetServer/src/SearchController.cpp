#include "LogSearchCommon.hpp"
#include "SearchController.hpp"

using Status = httplib::StatusCode;
using namespace std::chrono_literals;

void SearchController::HandlePostSearch(const httplib::Request& req, httplib::Response& res) {
    SearchConfig config;
    
    try {
        auto body = nlohmann::json::parse(req.body);

        // Populate config
        config.output_dir = body.value("outputDir", config.output_dir);

        std::string path = body.at("path").get<std::string>();
        
        uint64_t searchId = m_service.StartSearch(path, config);

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

namespace {
    /**
     * @brief Converts merged telemetry statistics to the JSON part of the status response.
     *
     * sum, min and max stay scaled integers (value * scale), so a client can compare them exactly.
     * min, max and mean are present only when at least one value was counted. After an overflow,
     * sum holds a wrapped value, so sum and mean are left out; count, errors, min and max stay valid.
     */
    nlohmann::json StatsToJson(const LogSystem::TelemetryStats& stats) {
        nlohmann::json json = {
            { "count",    stats.count },
            { "errors",   stats.errors },
            { "scale",    LogSystem::kValueScale },
            { "overflow", stats.overflow }
        };

        if (!stats.overflow) {
            json["sum"] = stats.sum;
        }

        if (stats.count > 0) {
            json["min"] = stats.min;
            json["max"] = stats.max;

            if (!stats.overflow) {
                json["mean"] = static_cast<double>(stats.sum) / static_cast<double>(stats.count)
                               / static_cast<double>(LogSystem::kValueScale);
            }
        }

        return json;
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
                { "scanErrors", session.scan_errors },
                { "stats", StatsToJson(session.stats) }
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