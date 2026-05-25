#include <iostream>
#include <filesystem>
#include <fstream>

#include "MasterServer.hpp"

// 10MB chunk size
const uint64_t CHUNK_SIZE = 10 * 1024 * 1024;

MasterServer::MasterServer(uint16_t port)
    : olc::net::server_interface<LogSystem::LogSearchMsg>(port) {}

void MasterServer::AddTask(const LogSystem::TaskPayload& task) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_pendingTasks.push_back(task);
    std::cout << "[MASTER] Added task for file: " << task.filename << "\n";
}

void MasterServer::StartSearch(const std::string& filepath, const std::string& keyword) {
    if (!std::filesystem::exists(filepath)) {
        std::cout << "[MASTER] Could not open desired file: " << filepath << "\n";
        return;
    }

    uint64_t fileSize = std::filesystem::file_size(filepath);
    uint64_t currentByte = 0;

    LogSystem::TaskPayload task;
    
    strncpy(task.filename, filepath.c_str(), sizeof(task.filename));
    task.filename[sizeof(task.filename) - 1] = '\0';

    strncpy(task.keyword, keyword.c_str(), sizeof(task.keyword));
    task.keyword[sizeof(task.keyword) - 1] = '\0';
    
    while (true) {
        if ((currentByte + CHUNK_SIZE) >= fileSize) {
            task.start_line = currentByte;
            task.end_line = fileSize;
            AddTask(task);
            break;
        }

        task.start_line = currentByte;
        task.end_line = currentByte + CHUNK_SIZE;

        AddTask(task);
        
        // Update next chunk start position
        currentByte += CHUNK_SIZE;
    }
}

bool MasterServer::OnClientConnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    std::cout << "[MASTER] New client connect with ID: " << client->GetID() << "\n";
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
            m_pendingTasks.push_front(it->second);

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
    if (workerToWakeUp)
        DispatchNextTask(workerToWakeUp);
}

void MasterServer::DispatchNextTask(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    if (!m_pendingTasks.empty()) {
        LogSystem::TaskPayload task = m_pendingTasks.front();
        m_pendingTasks.pop_front();

        uint32_t clientID = client->GetID();
        
        // Register task in unordered_map for Fault Tolerance
        m_inFlightTasks[clientID] = task;

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
    }
}

void MasterServer::OnMessage(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client, 
    olc::net::message<LogSystem::LogSearchMsg>& msg) {
    
    switch (msg.header.id) {
        case LogSystem::LogSearchMsg::Worker_Hello: {
            std::cout << "[MASTER] Hello message recived from Worker: " << client->GetID() << ". Asigning task\n";
            DispatchNextTask(client);

            break;
        }
        case LogSystem::LogSearchMsg::Worker_FoundLine: {
            LogSystem::ResultPayload result;
            msg >> result;

            // In here we should store found line and worker continues its job
            std::cout << "[MASTER] Worker: " << client->GetID() << ". Found New Line: " << result.text << "\n";

            break;
        }
        case LogSystem::LogSearchMsg::Worker_TaskDone: {
            std::cout << "[MASTER] Worker: " << client->GetID() << " finished asigned task\n";

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);

                auto it = m_inFlightTasks.find(client->GetID());
                if (it != m_inFlightTasks.end())
                    m_inFlightTasks.erase(it);
            }

            DispatchNextTask(client);
            
            break;
        }
        default:
            std::cout << "[MASTER] Undefined message type received from Worker: " << client->GetID() << "\n";
            break;
    }
}