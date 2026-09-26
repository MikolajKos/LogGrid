#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>

#include "MasterServer.hpp"

#define DOCKER_DEBUG

#ifdef DOCKER_DEBUG
    // 5 MB chunk size
    constexpr uint64_t CHUNK_SIZE = 5 * 1024 * 1024;
#else
    // 128 MB chunk size
    constexpr uint64_t CHUNK_SIZE = 128 * 1024 * 1024;
#endif


MasterServer::MasterServer(uint16_t port)
    : olc::net::server_interface<LogSystem::LogSearchMsg>(port) {
    const char* envInput = std::getenv("LOGGRID_INPUT_DIR");
    m_input_dir = envInput ? envInput : "/app/data";

    const char* envOutput = std::getenv("LOGGRID_OUTPUT_DIR");
    m_base_dir = envOutput ? envOutput : "/data/loggrid";
}

uint64_t MasterServer::StartSearch(const std::string& filepath, const std::string& keyword, const SearchConfig& config) {
    std::optional<uint64_t> optSearchId;

    try {
        std::filesystem::path basePath = std::filesystem::path(m_input_dir).lexically_normal();
        std::filesystem::path targetPath = (basePath / MakeRelative(filepath)).lexically_normal();

        std::string baseStr = basePath.string();
        if (!baseStr.ends_with('/')) baseStr += '/';

        if (targetPath.string() != basePath.string() && !targetPath.string().starts_with(baseStr))
            throw std::runtime_error("[MASTER] Security Alert: Path Traversal attempt blocked!");

        std::error_code ec;
        if (std::filesystem::is_symlink(targetPath, ec))
            throw std::runtime_error("[MASTER] Security Alert: Symlinks are not allowed: " + targetPath.string());
            
        if (!std::filesystem::exists(targetPath))
            throw std::runtime_error("[MASTER] Path not found: " + targetPath.string());
        
        uint64_t searchId = RegisterNewSession(config);
        optSearchId = searchId;
        
        if (std::filesystem::is_directory(targetPath.string())) {
            // Ignore permition denied and skip those files (avoids throwing error)
            auto options = std::filesystem::directory_options::skip_permission_denied;
            
            for (const auto& entry : std::filesystem::recursive_directory_iterator(targetPath, options)) {
                if (entry.is_regular_file(ec) && !entry.is_symlink(ec)) {
                    auto sz = entry.file_size(ec);
                    
                    if (!ec && sz > 0) {
                        std::string filePathStr = entry.path().string();
                            
                        // 5. Fix P1: Ochrona przed obcięciem zbyt długiej ścieżki
                        if (filePathStr.length() >= sizeof(LogSystem::TaskPayload::filename)) {
                            std::cout << "[MASTER] Warning: Path too long, skipping: " << filePathStr << "\n";
                            continue;
                        }
        
                        auto baseTask = CreateChunkTask(filePathStr, keyword, searchId, config);
                        EnqueueFileChunks(entry.path(), sz, baseTask);
                    }
                }
            }
        }
        else if (std::filesystem::is_regular_file(targetPath)) {
            auto sz = std::filesystem::file_size(targetPath, ec);
            
            if (ec)
                throw std::runtime_error("[MASTER] Failed to read file size: " + ec.message());

            if (sz > 0) {
                std::string filePathStr = targetPath.string();
            
                if (filePathStr.length() >= sizeof(LogSystem::TaskPayload::filename))
                    throw std::runtime_error("[MASTER] File path exceeds maximum length: " + filePathStr);
            
                auto baseTask = CreateChunkTask(filePathStr, keyword, searchId, config);
                EnqueueFileChunks(targetPath, sz, baseTask);
            }
        }
        else {
            throw std::runtime_error("[MASTER] Unsupported path type: " + filepath);
        }
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (optSearchId) m_sessions.erase(*optSearchId);
        throw;
    }

    // Wake up idle workers to process tasks
    AssignIdleWorkers();

    return *optSearchId;
}

void MasterServer::AssignIdleWorkers() {
    while (true) {
        std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> workerToWakeUp = nullptr;
        uint64_t threadsCount = 0;
    
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
    
            if (m_pendingTasks.empty() || m_idleWorkers.empty())
                break;
    
            workerToWakeUp = m_idleWorkers.front();
            m_idleWorkers.pop();
            m_idleWorkersIds.erase(workerToWakeUp->GetID());

            threadsCount = m_workersFreeSlots[workerToWakeUp->GetID()];
        }

        for (uint64_t i = 0; i < threadsCount; ++i) {
            if (!DispatchNextTask(workerToWakeUp))
                break;
        }
    }
}

void MasterServer::EnqueueFileChunks(const std::filesystem::path& filepath, const uint64_t fileSize, LogSystem::TaskPayload baseTask) {
    uint64_t currentByte = 0;
            
    std::lock_guard<std::mutex> lock(m_stateMutex);
    while (currentByte < fileSize) {
        baseTask.start_offset = currentByte;
        baseTask.end_offset = std::min(currentByte + CHUNK_SIZE, fileSize);
        
        m_pendingTasks.push_back(baseTask);
        m_sessions[baseTask.search_id].chunks_total++;
        
        currentByte += CHUNK_SIZE;
    }
}

LogSystem::TaskPayload MasterServer::CreateChunkTask(const std::string& filepath, const std::string& keyword, uint64_t searchId, const SearchConfig& config) {
    if (filepath.length() >= sizeof(LogSystem::TaskPayload::filename)) {
        throw std::runtime_error("[MASTER] Path exceeds maximum allowed length: " + filepath);
    }
    
    LogSystem::TaskPayload task;
    task.search_id = searchId;
    
    // Maximum count of result lines
    task.max_results = config.max_results;
    
    strncpy(task.filename, filepath.c_str(), sizeof(task.filename));
    task.filename[sizeof(task.filename) - 1] = '\0';
    
    strncpy(task.keyword, keyword.c_str(), sizeof(task.keyword));
    task.keyword[sizeof(task.keyword) - 1] = '\0';

    return task;
}

uint64_t MasterServer::RegisterNewSession(const SearchConfig& config) {
    uint64_t searchId;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        searchId = m_nextSearchId++;
    }
    
    SearchSession session;
    session.search_id = searchId;
    session.max_results = config.max_results;
    session.path = CreateSessionFilePath(config.output_dir, searchId);
    
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_sessions[searchId] = std::move(session);
    }

    return searchId;
}

std::optional<SearchStatus> MasterServer::GetStatus(const uint64_t search_id) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    auto it = m_sessions.find(search_id);

    // Session erased and completed
    if (it == m_sessions.end()) {
        return std::nullopt;
    }

    const auto& session = it->second;

    return SearchStatus {
        session.chunks_done == session.chunks_total ? SearchState::Done : SearchState::Running,
        session.line_count,
        session.total_matches,
        session.chunks_done,
        session.chunks_total
    };
}

bool MasterServer::OnClientConnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    std::cout << "[MASTER] New connection attempt\n";
    return true; // Accept the connection 
}

void MasterServer::OnClientDisconnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    uint32_t clientID = client->GetID();

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        
        // Adding back task from disconnected client
        auto it = m_inFlightTasks.find(clientID);
        
        if (it != m_inFlightTasks.end()) {
            for (const auto& task : it->second) {
                m_pendingTasks.push_front(task.second);
            }
            
            // Erasing old client because new one will be added
            m_inFlightTasks.erase(it);
            std::cout << "[MASTER] Fault Tolerance triggered. Reclaimed task from lost Worker ID: " << clientID << "\n";
        }
    }
    
    AssignIdleWorkers();
}

bool MasterServer::DispatchNextTask(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    auto idleWorkerIds_it = m_idleWorkersIds.find(client->GetID());
    
    if (!m_pendingTasks.empty()) {
        LogSystem::TaskPayload task = m_pendingTasks.front();
        m_pendingTasks.pop_front();

        uint32_t clientID = client->GetID();
        
        // Register task with task id for worker
        task.task_id = ++m_nextTaskId;
        m_inFlightTasks[clientID][m_nextTaskId] = task;

        m_workersFreeSlots[clientID]--;
        
        // --- CREATING MESSAGE ---
        olc::net::message<LogSystem::LogSearchMsg> msg;

        msg.header.id = LogSystem::LogSearchMsg::Server_SearchTask;

        // Message body
        msg << task;

        client->Send(msg);

        std::cout << "[MASTER] Dispatched task to Worker ID: " << clientID << "\n";

        return true;
    }
    else if (idleWorkerIds_it == m_idleWorkersIds.end()) {
        m_idleWorkers.push(client);
        m_idleWorkersIds.insert(client->GetID());

        std::cout << "[MASTER] No tasks available. Worker ID: " << client->GetID() << " added to idle queue.\n";
        
        return false; // <- To avoid adding worker to idle queue multiple times
    }

    return false;
}

void MasterServer::OnMessage(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client, 
    olc::net::message<LogSystem::LogSearchMsg>& msg) {
    
    switch (msg.header.id) {
        case LogSystem::LogSearchMsg::Worker_Hello: {
            std::cout << "[MASTER] Hello message recived from Worker: " << client->GetID() << ". Asigning task\n";

            // Available threads for worker
            uint64_t threadsCount;
            
            LogSystem::HelloMessage result;
            msg >> result;
            threadsCount = result.threads_available;

            m_workersFreeSlots[client->GetID()] = threadsCount;

            for (uint64_t i = 0; i < threadsCount; ++i) {
                // If threads > tasks available
                if (!DispatchNextTask(client)) {
                    break;
                }
            }

            break;
        }
        case LogSystem::LogSearchMsg::Worker_TaskDone: {
            std::cout << "[MASTER] Worker: " << client->GetID() << " finished asigned task\n";

            uint64_t searchId = 0;
            bool searchComplete = false;
            
            try {
                uint64_t taskId = AggregateTaskResult(msg);
                
                {
                    std::lock_guard<std::mutex> lock(m_stateMutex);

                    // New worker thread is free now
                    m_workersFreeSlots[client->GetID()]++;
                    
                    auto it = m_inFlightTasks.find(client->GetID());
                    if (it != m_inFlightTasks.end()) {
                        // Inner map contains all tasks assigned to a single worker
                        auto& innerMap = it->second;

                        searchId = innerMap[taskId].search_id;
                        innerMap.erase(taskId);
                        
                        if (innerMap.empty())
                            m_inFlightTasks.erase(it);

                        auto& session = m_sessions[searchId];
                        session.chunks_done++;
                    
                        if (session.chunks_done == session.chunks_total)
                            searchComplete = true;
                    }
                }

                DispatchNextTask(client);

                if (searchComplete) {
                    std::cout << "[MASTER] Search ID: " << searchId << " complete. Results delivered.\n";
                }
            }
            catch (const std::runtime_error& e) {
                std::cout << e.what() << "\n";
            }
            
            break;
        }
        default:
            std::cout << "[MASTER] Undefined message type received from Worker: " << client->GetID() << "\n";
            break;
    }
}

uint64_t MasterServer::AggregateTaskResult(olc::net::message<LogSystem::LogSearchMsg>& msg) {
    uint64_t taskId;
    // back message data is taskId
    msg >> taskId;
    
    LogSystem::ChunkResult batch = DeserializeBatch(msg);

    size_t linesToWrite = 0;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);

        auto it = m_sessions.find(batch.search_id);

        if (it == m_sessions.end()) {
            throw std::runtime_error("[MASTER] Unknown search_id:" + std::to_string(batch.search_id));
        }        
        
        auto& session = it->second;
        session.total_matches += batch.total_matches;
        
        if (session.line_count < session.max_results) {
            size_t needed = session.max_results - session.line_count;
            linesToWrite = std::min(batch.lines.size(), needed);
            
            session.line_count += linesToWrite;
        }
    }

    if (linesToWrite > 0) {
        if (batch.lines.size() > linesToWrite) {
            batch.lines.resize(linesToWrite);
        }
        WriteResults(batch.lines, batch.search_id);
    }

    
    return taskId;
}

LogSystem::ChunkResult MasterServer::DeserializeBatch(olc::net::message<LogSystem::LogSearchMsg>& msg) {
    LogSystem::ChunkResult batch;
    uint64_t searchId, totalMatches;
    uint32_t lineCount;

    // Read POD data from message back
    msg >> searchId >> totalMatches >> lineCount;

    batch.search_id = searchId;
    batch.total_matches = totalMatches;
    batch.lines_found = lineCount;
    
    std::vector<std::string> lines;
    
    // Read raw bytes (containing lenght and lines) from the front
    size_t offset = 0;
    for (uint32_t i = 0; i < lineCount; ++i) {
        uint32_t len;
        std::memcpy(&len, msg.body.data() + offset, 4); // Read 4 bytes
        offset += 4;

        lines.emplace_back(
            reinterpret_cast<const char*>(msg.body.data() + offset), len
        );

        offset += len;
    }

    batch.lines = std::move(lines);
    
    return batch;
}

void MasterServer::WriteResults(std::vector<std::string>& lines, const uint64_t searchId) {
    std::string fullPath;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        auto it = m_sessions.find(searchId);

        if (it == m_sessions.end()) {
            std::cout << "[MASTER] Session Not Found in WriteResults()";
            return;
        }

        fullPath = it->second.path;
    }
    
    auto optFile = OpenResultFile(fullPath);

    if (optFile) {
        auto file = std::move(*optFile);

        for (const auto& line : lines) {
            file << line << "\n";
        }
        
        file.close();
    }
}

std::optional<std::ofstream> MasterServer::OpenResultFile(const std::string& filename) {
    std::ofstream file(filename, std::ios::app | std::ios::binary);

    if (!file.is_open()) {
        std::cout << "[MASTER] Could not open the file: " << filename << "\n";
        return std::nullopt;
    }
    
    return file;
}

std::string MasterServer::CreateSessionFilePath(const std::string& userDir, const uint64_t searchId) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    std::string filename = std::format("search_{}_{}.log", searchId, timestamp);
    
    std::filesystem::path basePath = std::filesystem::path(m_base_dir).lexically_normal();
    std::filesystem::path relUserDir = MakeRelative(userDir);
    
    std::filesystem::path fullFilePath = (basePath / relUserDir / filename).lexically_normal();

    if (!fullFilePath.string().starts_with(basePath.string())) {
        throw std::runtime_error("[MASTER] Security Alert: Path Traversal attempt blocked!");
    }

    std::filesystem::create_directories(fullFilePath.parent_path());

    std::cout << "[MASTER] Result aggregation directory checked/created: " 
                << fullFilePath.parent_path().string() << "\n";

    return fullFilePath.string();
}

std::filesystem::path MasterServer::MakeRelative(std::string_view path) {
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.remove_prefix(1);
    }
    return path.empty() ? std::filesystem::path(".") : std::filesystem::path(path);
}