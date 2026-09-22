#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "WorkerClient.hpp"
#include "FileProcessor.hpp"

// Constructor with ThreadPool initialization
WorkerClient::WorkerClient() 
    : m_threadPool(CalculateThreadCount(std::thread::hardware_concurrency())) {}

size_t WorkerClient::CalculateThreadCount(size_t threadsAvailable) {
    // prevent CPU oversubscription (N-1 Rule)
    if (threadsAvailable > 1) threadsAvailable -= 1;

    return threadsAvailable == 0 ? 4 : threadsAvailable;
}

void WorkerClient::OnMessage(olc::net::message<LogSystem::LogSearchMsg>& msg) {
    switch (msg.header.id) {
        case LogSystem::LogSearchMsg::Server_SearchTask: {
            std::cout << "[WORKER] New task was received\n";
            
            LogSystem::TaskPayload task;
            msg >> task;

            // Pass processing to a thread
            m_threadPool.Enqueue([this, task]() {
                // searchId for session identification
                uint64_t searchId = task.search_id;
                
                // Task id for TaskDone message
                uint64_t taskId = task.task_id;
                uint64_t maxLineCount = task.max_results;

                auto batch = FileProcessor::SearchTask(task, maxLineCount);

                batch.search_id = searchId;
                batch.task_id = taskId;
                
                SendTaskDone(batch);
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

void WorkerClient::SendHello() {
    olc::net::message<LogSystem::LogSearchMsg> msg;
    msg.header.id = LogSystem::LogSearchMsg::Worker_Hello;
    
    // Create payload
    LogSystem::HelloMessage payload;
    payload.threads_available = m_threadPool.Size();

    msg << payload;

    Send(msg);
}

void WorkerClient::SendTaskDone(LogSystem::ChunkResult& batch) {
    auto msg = SerializeBatch(batch);
    msg.header.id = LogSystem::LogSearchMsg::Worker_TaskDone;

    Send(msg);
}

olc::net::message<LogSystem::LogSearchMsg> WorkerClient::SerializeBatch(LogSystem::ChunkResult& batch) {
    olc::net::message<LogSystem::LogSearchMsg> msg;

    // Serialize string vector
    for (const auto& line : batch.lines) {
        uint32_t len = line.size();
        const uint8_t* lenPtr = reinterpret_cast<const uint8_t*>(&len);
        msg.body.insert(msg.body.end(), lenPtr, lenPtr + 4);
        msg.body.insert(msg.body.end(), line.begin(), line.end());
    }
    msg.header.size = msg.size();

    // PODs
    msg << batch.lines_found
        << batch.total_matches
        << batch.search_id
        << batch.task_id;
    
    return msg;
}

void WorkerClient::OnConnectionResult(bool bConnected) {
    // [ACK] Message
    if (bConnected) {
        std::cout << "[WORKER] Connection established\n";
        SendHello();
    }
    else {
        std::cout << "[WORKER] Could not connect to the server\n";
    }
}

bool WorkerClient::ShouldDisconnect() {
    return m_shouldDisconnect;
}