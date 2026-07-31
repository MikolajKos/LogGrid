#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "WorkerClient.hpp"
#include "FileProcessor.hpp"

// Constructor with ThreadPool initialization
WorkerClient::WorkerClient()
    : m_threadPool(std::thread::hardware_concurrency() - 1) {}

void WorkerClient::OnMessage(olc::net::message<LogSystem::LogSearchMsg>& msg) {
    switch (msg.header.id) {
        case LogSystem::LogSearchMsg::Server_SearchTask: {
            std::cout << "[WORKER] New task was received\n";
            
            LogSystem::TaskPayload task;
            msg >> task;
            
            // Pass processing to a thread
            m_threadPool.Enqueue([this, task]() {
                uint64_t searchId = task.search_id;
                
                FileProcessor::SearchTask(
                    task,
                    [this, searchId](const std::string& line) { this->SendMessage(LogSystem::LogSearchMsg::Worker_FoundLine, line, searchId); },
                    [this]() { this->SendMessage(LogSystem::LogSearchMsg::Worker_TaskDone); }
                );
            });

            break;
        }
        case LogSystem::LogSearchMsg::Server_JobFinished: {
            std::cout << "[WORKER] Job finished message received. Shutting down...\n";
            m_shouldDisconnect = true;

            break;
        }
        default: {
            std::cout << "[WORKER] Undefined message type received\n";
            break;
        }
    }
}

void WorkerClient::SendMessage(const LogSystem::LogSearchMsg msgType, const std::string& line, const uint64_t searchId) {
    olc::net::message<LogSystem::LogSearchMsg> msg;

    msg.header.id = msgType;
    
    if (!line.empty()) {
        // Create payload structure
        LogSystem::ResultPayload payload;

        // Important to leave one free byte for null-terminator '\0'
        strncpy(payload.text, line.c_str(), sizeof(payload.text) - 1);
        
        // sizeof - 1 because 255 is last index not 256! Rookie mistake
        payload.text[sizeof(payload.text) - 1] = '\0';

        payload.search_id = searchId;
        
        msg << payload;
    }

    Send(msg);
}

void WorkerClient::SendHelloMessage() {
    olc::net::message<LogSystem::LogSearchMsg> msg;
    msg.header.id = LogSystem::LogSearchMsg::Worker_Hello;
    
    // Create payload
    LogSystem::HelloMessage payload;
    payload.threads_available = m_threadPool.Size();

    msg << payload;

    Send(msg);
}

void WorkerClient::OnConnectionResult(bool bConnected) {
    // [ACK] Message
    if (bConnected) {
        std::cout << "[WORKER] Connection established\n";
        SendHelloMessage();
    }
    else {
        std::cout << "[WORKER] Could not connect to the server\n";
    }
}

bool WorkerClient::ShouldDisconnect() {
    return m_shouldDisconnect;
}