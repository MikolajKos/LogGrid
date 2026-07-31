#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>

#include "MasterServer.hpp"

#define DOCKER_DEBUG

#ifdef DOCKER_DEBUG
    // 10 KB chunk size
    const uint64_t CHUNK_SIZE = 10 * 1024;
#else
    // 10MB chunk size
    const uint64_t CHUNK_SIZE = 10 * 1024 * 1024;
#endif


MasterServer::MasterServer(uint16_t port)
    : olc::net::server_interface<LogSystem::LogSearchMsg>(port) {}

std::future<LogSystem::SearchResult> MasterServer::StartSearch(const std::string& filepath, const std::string& keyword) {
    if (!std::filesystem::exists(filepath))
        throw std::runtime_error("[MASTER] File not found: " + filepath);

    // Set Search Session
    SearchSession session;

    session.result.search_id = m_nextSearchId;    
    std::future<LogSystem::SearchResult> future = session.promise.get_future();
    
    m_sessions[m_nextSearchId] = std::move(session);

    uint64_t fileSize = std::filesystem::file_size(filepath);
    uint64_t currentByte = 0;

    m_sessions[m_nextSearchId].chunks_total = (fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE;

    LogSystem::TaskPayload task;
    
    task.search_id = m_nextSearchId;

    strncpy(task.filename, filepath.c_str(), sizeof(task.filename));
    task.filename[sizeof(task.filename) - 1] = '\0';

    strncpy(task.keyword, keyword.c_str(), sizeof(task.keyword));
    task.keyword[sizeof(task.keyword) - 1] = '\0';

    std::vector<std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>>> idleWorkers;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);

        while (true) {
            if ((currentByte + CHUNK_SIZE) >= fileSize) {
                task.start_line = currentByte;
                task.end_line = fileSize;
                m_pendingTasks.push_back(task);
                break;
            }

            task.start_line = currentByte;
            task.end_line = currentByte + CHUNK_SIZE;
            m_pendingTasks.push_back(task);
            
            // Update next chunk start position
            currentByte += CHUNK_SIZE;
        }

        while (!m_idleWorkers.empty()) {
            idleWorkers.push_back(m_idleWorkers.front());
            m_idleWorkers.pop();
        }
    }

    m_nextSearchId++;
    for (auto worker : idleWorkers) {
        DispatchNextTask(worker);
    }

    return future;
}

bool MasterServer::OnClientConnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    std::cout << "[MASTER] New connection attempt\n";
    return true; // Accept the connection 
}

void MasterServer::OnClientDisconnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    uint32_t clientID = client->GetID();

    std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> workerToWakeUp = nullptr;
    
    // Scope locked block
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

            if (!m_idleWorkers.empty()) {
                workerToWakeUp = m_idleWorkers.front();
                m_idleWorkers.pop();
            }
        }
    }

    // Asign job to new worker if exists
    if (workerToWakeUp) {
        uint64_t threadsCount = m_workersThreads[workerToWakeUp->GetID()];

        for (uint64_t i = 0; i < threadsCount; ++i) {
            DispatchNextTask(workerToWakeUp);
        }
    }
}

void MasterServer::DispatchNextTask(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    if (!m_pendingTasks.empty()) {
        LogSystem::TaskPayload task = m_pendingTasks.front();
        m_pendingTasks.pop_front();

        uint32_t clientID = client->GetID();
        
        // Register task with task id for worker
        task.task_id = ++m_nextTaskId;
        m_inFlightTasks[clientID][m_nextTaskId] = task;

        // --- CREATING MESSAGE ---
        olc::net::message<LogSystem::LogSearchMsg> msg;

        msg.header.id = LogSystem::LogSearchMsg::Server_SearchTask;

        // Message body
        msg << task;

        client->Send(msg);

        std::cout << "[MASTER] Dispatched task to Worker ID: " << clientID << "\n";
    }
    else if (!m_inFlightTasks.empty()) {
        m_idleWorkers.push(client);

        std::cout << "[MASTER] No tasks available. Worker ID: " << client->GetID() << " added to idle queue.\n";
    }
    else {
        // Tell the client that the job is done
        olc::net::message<LogSystem::LogSearchMsg> msg;

        msg.header.id = LogSystem::LogSearchMsg::Server_JobFinished;

        client->Send(msg);
        std::cout << "[MASTER] No more tasks. Notified Worker ID: " << client->GetID() << " to shut down.\n";

        while (!m_idleWorkers.empty()) {
            auto idleClient = m_idleWorkers.front();
            m_idleWorkers.pop();

            idleClient->Send(msg);
            std::cout << "[MASTER] Waking up idle Worker ID: " << idleClient->GetID() << " to shut down\n";
        }

        std::cout << "[MASTER] All workers shut down. Search is COMPLETE!\n";
    }
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

            m_workersThreads[client->GetID()] = threadsCount;

            for (uint64_t i = 0; i < threadsCount; ++i) {
                DispatchNextTask(client);
            }

            break;
        }
        case LogSystem::LogSearchMsg::Worker_FoundLine: {
            LogSystem::ResultPayload result;
            msg >> result;

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                auto it = m_sessions.find(result.search_id);
                if (it != m_sessions.end())
                    it->second.result.lines.push_back(result.text);
                else
                    std::cout << "[MASTER] Worker_FoundLine: unknown search_id: " << result.search_id << "\n";
            }

            break;
        }
        case LogSystem::LogSearchMsg::Worker_TaskDone: {
            std::cout << "[MASTER] Worker: " << client->GetID() << " finished asigned task\n";

            // Read message containing task id
            LogSystem::TaskDoneResult result;
            msg >> result;

            uint64_t taskId = result.task_id;

            uint64_t searchId = 0;
            bool searchComplete = false;
            LogSystem::SearchResult completedResult;
            std::promise<LogSystem::SearchResult> completedPromise;

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);

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

                    if (session.chunks_done == session.chunks_total) {
                        searchComplete = true;
                        completedResult = std::move(session.result);
                        completedPromise = std::move(session.promise);

                        m_sessions.erase(searchId);
                    }
                }
            }

            DispatchNextTask(client);

            if (searchComplete) {
                completedPromise.set_value(std::move(completedResult));
                std::cout << "[MASTER] Search ID: " << searchId << " complete. Results delivered.\n";
            }
            
            break;
        }
        default:
            std::cout << "[MASTER] Undefined message type received from Worker: " << client->GetID() << "\n";
            break;
    }
}
