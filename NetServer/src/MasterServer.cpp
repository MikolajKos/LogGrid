#include <iostream>

#include "MasterServer.hpp"

MasterServer::MasterServer(uint16_t port)
    : olc::net::server_interface<LogSystem::LogSearchMsg>(port) {}

void MasterServer::AddTask(const LogSystem::TaskPayload& task) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_pendingTasks.push_back(task);
    std::cout << "[MASTER] Added task for file: " << task.filename << "\n";
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