#ifndef WORKER_CLIENT_HPP
#define WORKER_CLIENT_HPP

#include <atomic>

#include "LogSearchCommon.hpp"
#include "olc_net.hpp"
#include "ThreadPool.hpp"

class WorkerClient : public olc::net::client_interface<LogSystem::LogSearchMsg> {
public:
    WorkerClient();
    virtual ~WorkerClient() = default;
    static constexpr size_t kMaxThreads = 256;

protected:
    void OnConnectionResult(bool bConnected) override;
    void OnMessage(olc::net::message<LogSystem::LogSearchMsg>& msg) override;

public:
    bool ShouldDisconnect();
    static size_t CalculateThreadCount(const char* envValue, size_t hardwareThreads);
private:
    void SendFoundLine(const LogSystem::LogSearchMsg msgType, const std::string& line = std::string(), const uint64_t searchId = 0);
    void SendHello();
    void SendTaskDone(LogSystem::ChunkResult& batch);
    olc::net::message<LogSystem::LogSearchMsg> SerializeBatch(LogSystem::ChunkResult& batch);
    static const char* GetThreadsEnv();
private:
    ThreadPool m_threadPool;
    std::atomic<bool> m_shouldDisconnect = false;
};

#endif // WORKER_CLIENT_HPP