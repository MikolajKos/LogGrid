#ifndef WORKER_CLIENT_HPP
#define WORKER_CLIENT_HPP

#include "olc_net.hpp"
#include "LogSearchCommon.hpp"

class WorkerClient : public olc::net::client_interface<LogSystem::LogSearchMsg> {
public:
    WorkerClient();
    virtual ~WorkerClient() = default;

protected:
    void OnConnectionResult(bool bConnected) override;
    void OnMessage(olc::net::message<LogSystem::LogSearchMsg>& msg) override;

private:
    void ProcessTask(const LogSystem::TaskPayload& task);

public:
    void SendMessage(const LogSystem::LogSearchMsg msgType, const std::string& line = std::string());
};

#endif // WORKER_CLIENT_HPP