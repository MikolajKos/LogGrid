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

uint64_t MasterServer::StartSearch(const std::string& filepath, const SearchConfig& config) {
    std::error_code ec;

    // Real location of the input root, with every symlink resolved
    std::filesystem::path baseCanon = std::filesystem::canonical(m_input_dir, ec);
    if (ec)
        throw std::runtime_error("[MASTER] Input directory unavailable: " + m_input_dir);

    // Real location of the target; also fails if the path does not exist
    std::filesystem::path targetCanon = std::filesystem::canonical(baseCanon / MakeRelative(filepath), ec);
    if (ec)
        throw std::runtime_error("[MASTER] Path not found: " + filepath);

    // Trailing separator rejects siblings such as /app/data-private
    std::string baseStr = baseCanon.string();
    if (!baseStr.ends_with('/')) baseStr += '/';

    if (targetCanon != baseCanon && !targetCanon.string().starts_with(baseStr)) {
        std::cout << "[MASTER] Security Alert: Path Traversal attempt blocked! Path: " << filepath << "\n";
        throw std::runtime_error("[MASTER] Path not found: " + filepath);
    }

    uint64_t searchId = AllocateSearchId();
    SearchSession session = BuildSession(searchId, config);

    // Helper vector for collecting chunks from all files before enqueuing them on m_pendingTasks
    std::vector<LogSystem::TaskPayload> newTasks;

    if (std::filesystem::is_directory(targetCanon)) {
        ScanDirectoryRecursively(targetCanon, searchId, newTasks, session.scan_errors);
    }
    else if (std::filesystem::is_regular_file(targetCanon)) {
        if(!ProcessSingleFile(targetCanon, searchId, newTasks))
            throw std::runtime_error("[MASTER] Error occurred while reading a file: " + filepath);
    }
    else {
        throw std::runtime_error("[MASTER] Unsupported path type: " + filepath);
    }

    // Nothing could be searched and something failed
    if (newTasks.empty() && session.scan_errors > 0)
        throw std::runtime_error("[MASTER] Could not scan path: " + filepath);
    
    AddPendingTasks(std::move(session), newTasks);

    // Wake up idle workers to process tasks
    AssignIdleWorkers();

    return searchId;
}

void MasterServer::ScanDirectoryRecursively(const std::filesystem::path& dir, uint64_t searchId, std::vector<LogSystem::TaskPayload>& outTasks, uint64_t& scanErrors) {
    std::error_code ec;
    auto it = std::filesystem::directory_iterator(dir, ec);

    if (ec) {
        std::cout << "[MASTER] Scan error: cannot open directory " << dir.string() << ": " << ec.message() << "\n";
        ++scanErrors;
        return;
    }

    for (auto end = std::filesystem::directory_iterator(); it != end; it.increment(ec)) {
        
        auto& entry = *it;
        
        if (entry.is_symlink(ec) || ec) {
            if (ec) {
                std::cout << "[MASTER] Scan error: cannot examine " << entry.path().string() << ": " << ec.message() << "\n";
                ++scanErrors;
            }
            continue;
        }
        
        if (entry.is_directory(ec)) {
            ScanDirectoryRecursively(entry.path(), searchId, outTasks, scanErrors);
        }
        else if (entry.is_regular_file(ec)) {
            if (!ProcessSingleFile(entry.path(), searchId, outTasks))
                ++scanErrors;
        }
        else if (ec) {
            std::cout << "[MASTER] Scan error: cannot examine " << entry.path().string() << ": " << ec.message() << "\n";
            ++scanErrors;
        }
    }

    // A failed increment makes the iterator equal to end, so the rest of this directory is lost
    if (ec) {
        std::cout << "[MASTER] Scan error: listing of " << dir.string() << " stopped early: " << ec.message() << "\n";
        ++scanErrors;
    }
}

bool MasterServer::ProcessSingleFile(const std::filesystem::path& targetPath, uint64_t searchId, std::vector<LogSystem::TaskPayload>& outTasks) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(targetPath, ec);
    
    std::string filePathStr = targetPath.string();

    if (ec) {
        std::cout << "[MASTER] Error occurred while reading a file: " << filePathStr << "\n";
        return false;
    }

    // Empty file is valid
    if (sz == 0) return true;
    
    if (filePathStr.length() >= sizeof(LogSystem::TaskPayload::filename)) {
        std::cout << "[MASTER] Warning: Path too long, skipping: " << filePathStr << "\n";
        return false;
    } else {
        auto baseTask = CreateChunkTask(filePathStr, searchId);
        EnqueueFileChunks(targetPath, sz, baseTask, outTasks);
        return true;
    }
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

            auto itSlots = m_workersFreeSlots.find(workerToWakeUp->GetID());
            
            // Skip deleted or disconnected workers
            if (itSlots == m_workersFreeSlots.end())
                continue;

            m_idleWorkersIds.erase(workerToWakeUp->GetID());
            threadsCount = m_workersFreeSlots[workerToWakeUp->GetID()];
        }

        for (uint64_t i = 0; i < threadsCount; ++i) {
            if (!DispatchNextTask(workerToWakeUp))
                break;
        }
    }
}

void MasterServer::AddPendingTasks(SearchSession&& session, const std::vector<LogSystem::TaskPayload>& newTasks) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    session.chunks_total = newTasks.size();
    m_sessions[session.search_id] = std::move(session);
    m_pendingTasks.insert(m_pendingTasks.end(), newTasks.begin(), newTasks.end());
}

void MasterServer::EnqueueFileChunks(const std::filesystem::path& filepath, const uint64_t fileSize, LogSystem::TaskPayload baseTask, std::vector<LogSystem::TaskPayload>& outTasks) {
    uint64_t currentByte = 0;

    while (currentByte < fileSize) {
        baseTask.start_offset = currentByte;
        baseTask.end_offset = std::min(currentByte + CHUNK_SIZE, fileSize);
        outTasks.push_back(baseTask);
        currentByte += CHUNK_SIZE;
    }
}

LogSystem::TaskPayload MasterServer::CreateChunkTask(const std::string& filepath, uint64_t searchId) {
    if (filepath.length() >= sizeof(LogSystem::TaskPayload::filename)) {
        throw std::runtime_error("[MASTER] Path exceeds maximum allowed length: " + filepath);
    }
    
    LogSystem::TaskPayload task{};
    task.search_id = searchId;
    
    strncpy(task.filename, filepath.c_str(), sizeof(task.filename));
    task.filename[sizeof(task.filename) - 1] = '\0';

    return task;
}

uint64_t MasterServer::AllocateSearchId() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_nextSearchId++;
}

SearchSession MasterServer::BuildSession(const uint64_t searchId, const SearchConfig& config) {
    SearchSession session;
    session.search_id = searchId;
    session.path = CreateSessionFilePath(config.output_dir, searchId);
    
    return session;
}

std::optional<SearchStatus> MasterServer::GetStatus(const uint64_t search_id) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    auto it = m_sessions.find(search_id);

    // Unknown search_id
    if (it == m_sessions.end()) {
        return std::nullopt;
    }

    const auto& session = it->second;

    return SearchStatus {
        .stats        = session.stats,
        .state        = session.chunks_done == session.chunks_total ? SearchState::Done : SearchState::Running,
        .scan_errors  = session.scan_errors,
        .chunks_done  = session.chunks_done,
        .chunks_total = session.chunks_total
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
        
        // Erase disconnected client
        m_workersFreeSlots.erase(clientID);
        m_idleWorkersIds.erase(clientID);
        
        // Adding back task from disconnected client
        auto it = m_inFlightTasks.find(clientID);
        
        if (it != m_inFlightTasks.end()) {
            for (const auto& task : it->second) {
                m_pendingTasks.push_front(task.second);
            }
            
            // Erasing old client because new one will be woken up
            m_inFlightTasks.erase(it);
            std::cout << "[MASTER] Fault Tolerance triggered. Reclaimed task from lost Worker ID: " << clientID << "\n";
        }
    }
    
    AssignIdleWorkers();
}

bool MasterServer::DispatchNextTask(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    if (!m_workersFreeSlots.count(client->GetID()))
        return false;
    
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
    
    try {
        switch (msg.header.id) {
            case LogSystem::LogSearchMsg::Worker_Hello: {
                std::cout << "[MASTER] Hello message recived from Worker: " << client->GetID() << ". Asigning task\n";

                // Available threads for worker
                uint64_t threadsCount;
                
                LogSystem::HelloMessage result;
                msg >> result;
                threadsCount = result.threads_available;

                {
                    std::lock_guard<std::mutex> lock(m_stateMutex);
                    m_workersFreeSlots[client->GetID()] = threadsCount;
                }

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
                
                
                LogSystem::ChunkResult batch;
                msg >> batch;
                uint64_t taskId = batch.task_id;
                
                uint64_t authoritativeSearchId = 0;
                bool taskExists = false;
                
                {
                    std::lock_guard<std::mutex> lock(m_stateMutex);
                    auto it = m_inFlightTasks.find(client->GetID());
                    if (it != m_inFlightTasks.end() && it->second.count(taskId)) {
                        authoritativeSearchId = it->second[taskId].search_id;
                        taskExists = true;
                        
                        it->second.erase(taskId);
                        if (it->second.empty())
                        m_inFlightTasks.erase(it);

                        m_workersFreeSlots[client->GetID()]++;
                    }
                }
                
                if (!taskExists) {
                    std::cout << "[MASTER] Received unknown taskId from Worker.\n";
                    break;
                }
                
                // Dispatch the next task before merging, so the Worker does not wait for it
                DispatchNextTask(client);
                
                try {
                    AggregateTaskResult(batch, authoritativeSearchId);
                }
                catch(const std::exception& e) {
                    std::cout << "[MASTER] Aggregation error: " << e.what() << "\n";
                }
                
                // The chunk is counted as done even if the merge above failed (see KNOWN_ISSUES.md)
                bool searchComplete = false;
                {
                    std::lock_guard<std::mutex> lock(m_stateMutex);
                    if (m_sessions.count(authoritativeSearchId)) {
                        auto& session = m_sessions[authoritativeSearchId];
                        session.chunks_done++;

                        if (session.chunks_done == session.chunks_total)
                            searchComplete = true;
                    }
                }


                if (searchComplete) {
                    std::cout << "[MASTER] Search ID: " << authoritativeSearchId << " complete.\n";
                }

                break;
            }
            default:
                std::cout << "[MASTER] Undefined message type received from Worker: " << client->GetID() << "\n";
                break;
        }
    }
    catch (const std::exception& e) {
        std::cout << "[MASTER] Malformed message from Worker " << client->GetID()
            << ": " << e.what() << "\n";
    }
}

void MasterServer::AggregateTaskResult(const LogSystem::ChunkResult& batch, const uint64_t authoritativeSearchId) {
        std::lock_guard<std::mutex> lock(m_stateMutex);

        auto it = m_sessions.find(authoritativeSearchId);
        if (it == m_sessions.end()) {
            throw std::runtime_error("[MASTER] Unknown search_id:" + std::to_string(authoritativeSearchId));
        }        

        it->second.stats.Merge(batch.stats);
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