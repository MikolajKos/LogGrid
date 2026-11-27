#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "WorkerClient.hpp"
#include "FileProcessor.hpp"

// Constructor with ThreadPool initialization
WorkerClient::WorkerClient()
    : m_threadPool(CalculateThreadCount(GetThreadsEnv(), std::thread::hardware_concurrency())) {
    std::cout << "[WORKER] Using " << m_threadPool.Size() << " threads\n";
}

const char* WorkerClient::GetThreadsEnv() {
    return std::getenv("LOGGRID_WORKER_THREADS");
}

size_t WorkerClient::CalculateThreadCount(const char* envValue, size_t hardwareThreads) {
    // Not set, empty or "0" means auto: use the hardware thread count below
    if (envValue && *envValue != '\0') {
        size_t parsed = 0;
        const char* end = envValue + std::strlen(envValue);
        auto [ptr, ec] = std::from_chars(envValue, end, parsed);

        if (ec == std::errc{} && ptr == end && parsed <= kMaxThreads) {
            // "0" (also "00") means auto: fall through to the hardware thread count
            if (parsed > 0) {
                return parsed;
            }
        }
        else {
            std::cout << "[WORKER] Invalid LOGGRID_WORKER_THREADS value: \"" << envValue
                      << "\", using hardware thread count\n";
        }
    }

    // prevent CPU oversubscription (N-1 Rule)
    if (hardwareThreads > 1) --hardwareThreads;
    return hardwareThreads == 0 ? 4 : hardwareThreads;
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
                
                auto batch = FileProcessor::ProcessChunk(task);

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

void WorkerClient::SendTaskDone(const LogSystem::ChunkResult& batch) {
    olc::net::message<LogSystem::LogSearchMsg> msg;
    msg.header.id = LogSystem::LogSearchMsg::Worker_TaskDone;
    msg << batch;

    Send(msg);
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