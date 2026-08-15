#ifndef TEST_SERVER_HPP
#define TEST_SERVER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <mutex>

#include "LogSearchCommon.hpp"
#include "olc_net.hpp"


class TestServer : public olc::net::server_interface<LogSystem::LogSearchMsg> {
public:
    explicit TestServer(uint16_t port) :
        olc::net::server_interface<LogSystem::LogSearchMsg>(port) {};
        
        virtual ~TestServer() {
            m_running = false;
        };
public:
    bool Start() {
        if (!server_interface::Start()) return false;

        m_running = true;
        m_updateThread = std::jthread([this]() {
            while (m_running)
                Update();
        });

        return true;
    }

    void SendTask(LogSystem::TaskPayload payload) {
        if (!m_connectedClient) return;

        m_sentTaskId = payload.task_id;
        
        olc::net::message<LogSystem::LogSearchMsg> msg;
        msg.header.id = LogSystem::LogSearchMsg::Server_SearchTask;
        msg << payload;

        m_connectedClient->Send(msg);
    }

    bool WaitForTaskDone(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_condVarTaskDone.wait_for(lock, timeout, [this]{ return m_taskDoneReceived; });
    }    
    
    bool WaitForHello(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_conditionVar.wait_for(lock, timeout, [this]{ return m_helloReceived; });
    }

    bool WaitForFoundLine(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_condVarFoundLine.wait_for(lock, timeout, [this]{ return m_lineReceived; });
    }

    void ResetFlags() {
        m_sentTaskId = 0;
        m_receivedTaskId = 0;

        m_helloReceived = false;
        m_taskDoneReceived = false;
        m_lineReceived = false;

        m_resultLine = "";
    }

    bool HelloReceived() const { return m_helloReceived; }

    bool ReceivedTaskDoneIdMatch() const { return m_sentTaskId == m_receivedTaskId; }

    std::string GetFoundLineResult() const { return m_resultLine; }

protected:
    bool OnClientConnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) override {
        m_connectedClient = client;
        return true;
    }
    
    void OnClientDisconnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) override {
        
    }
    
    void OnMessage(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client, olc::net::message<LogSystem::LogSearchMsg>& msg) override {
        switch (msg.header.id)
        {
            case LogSystem::LogSearchMsg::Worker_Hello: {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_helloReceived = true;
                }
                m_conditionVar.notify_one();
                
                break;
            }
            case LogSystem::LogSearchMsg::Worker_FoundLine:{
                LogSystem::ResultPayload result;
                msg >> result;
                m_resultLine = result.text;
                
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_lineReceived = true;
                }
                m_condVarFoundLine.notify_one();
                
                break;
            }
            case LogSystem::LogSearchMsg::Worker_TaskDone:{
                LogSystem::TaskDoneResult result;
                msg >> result;

                m_receivedTaskId = result.task_id;
                
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_taskDoneReceived = true;
                }
                m_condVarTaskDone.notify_one();
                
                break;
            }
            default:
            break;
        }
    }
    
private:
    std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> m_connectedClient = nullptr;

    // ACK test flag
    bool m_helloReceived = false;

    // Task done matching task id fields
    uint64_t m_sentTaskId = 0;
    uint64_t m_receivedTaskId = 0;
    bool m_taskDoneReceived = false;

    // Worker found line test fields
    bool m_lineReceived = false;
    std::string m_resultLine = "";
    std::condition_variable m_condVarFoundLine;
    
    std::mutex m_mutex;    
    std::condition_variable m_conditionVar;
    std::condition_variable m_condVarTaskDone;

    std::atomic<bool> m_running;
    std::jthread m_updateThread;
};

#endif // TEST_SERVER_HPP