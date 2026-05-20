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