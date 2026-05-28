#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "WorkerClient.hpp"
#include "FileProcessor.hpp"

// Constructor with ThreadPool initialization
WorkerClient::WorkerClient()
    : m_threadPool(std::thread::hardware_concurrency()) {}

void WorkerClient::OnMessage(olc::net::message<LogSystem::LogSearchMsg>& msg) {
    switch (msg.header.id) {
        case LogSystem::LogSearchMsg::Server_SearchTask: {
            std::cout << "[WORKER] New task was received\n";
            
            LogSystem::TaskPayload task;
            msg >> task;
            
            // Pass processing to a thread
            m_threadPool.Enqueue([this, task]() {
                FileProcessor::SearchTask(
                    task,
                    [this](const std::string& line) { this->SendMessage(LogSystem::LogSearchMsg::Worker_FoundLine, line); },
                    [this]() { this->SendMessage(LogSystem::LogSearchMsg::Worker_TaskDone); }
                );
            });

            break;
        }
        case LogSystem::LogSearchMsg::Server_JobFinished: {
            break;
        }
        default: {
            std::cout << "[WORKER] Undefined message type received\n";
            break;
        }
    }
}

void WorkerClient::SendMessage(const LogSystem::LogSearchMsg msgType, const std::string& line) {
    olc::net::message<LogSystem::LogSearchMsg> msg;

    msg.header.id = msgType;

    if (!line.empty()) {
        // Create payload structure
        LogSystem::ResultPayload payload;

        // Important to leave one free byte for null-terminator '\0'
        strncpy(payload.text, line.c_str(), sizeof(payload.text) - 1);
        
        // sizeof - 1 because 255 is last index not 256! Rookie mistake
        payload.text[sizeof(payload.text) - 1] = '\0';

        msg << payload;
    }

    Send(msg);
}

void WorkerClient::OnConnectionResult(bool bConnected) {

}