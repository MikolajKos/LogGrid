


#ifndef WORKER_CLIENT_HPP
#define WORKER_CLIENT_HPP

#include "olc_net.hpp"
#include "LogSearchCommon.hpp"

class WorkerClient : public olc::net::client_interface<LogSystem::LogSearchMsg> {
public:
    WorkerClient();

protected:
    void OnConnectionResult(bool bConnected) override;
    void OnMessage(olc::net::message<LogSystem::LogSearchMsg>& msg) override;

public:
    void ProcessTask(const LogSystem::TaskPayload& task);
    
private:
    void SendMessage(const LogSystem::LogSearchMsg msgType, msg);
};

#endif // WORKER_CLIENT_HPP