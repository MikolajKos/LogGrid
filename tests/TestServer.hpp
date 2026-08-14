#ifndef TEST_SERVER_HPP
#define TEST_SERVER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "LogSearchCommon.hpp"
#include "olc_net.hpp"


class TestServer : public olc::net::server_interface<LogSystem::LogSearchMsg> {
public:
    explicit TestServer(uint16_t port) :
        olc::net::server_interface<LogSystem::LogSearchMsg>(port) {};
        
        virtual ~TestServer() = default;
public:
    bool WaitForHello(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_conditionVar.wait_for(lock, timeout, [this]{ return m_helloReceived; });
    }

    void ResetFlags() {
        m_helloReceived = false;
    }

    bool HelloReceived() const { return m_helloReceived; }

protected:
    bool OnClientConnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) override {
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
                break;
            }
            case LogSystem::LogSearchMsg::Worker_TaskDone:{
                break;
            }
            default:
            break;
        }
    }
    
private:
    bool m_helloReceived = false;
    std::mutex m_mutex;
    std::condition_variable m_conditionVar;
};

#endif // TEST_SERVER_HPP