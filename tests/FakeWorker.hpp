#ifndef FAKE_WORKER_HPP
#define FAKE_WORKER_HPP

#include "LogSearchCommon.hpp"

#include <chrono>
#include <optional>
#include <string_view>
#include <string>

class FakeWorker {
public:
    FakeWorker() : m_socket(m_context) {};

    bool Connect(std::string_view host, const uint16_t port) {
        asio::ip::tcp::resolver resolver(m_context);
        std::error_code ec;
        
        auto endpoints = resolver.resolve(host, std::to_string(port), ec);

        if (ec) return false;

        asio::connect(m_socket, endpoints, ec);

        return ec ? false : true;
    }

    void SendHello(uint64_t threads) {
        olc::net::message<LogSystem::LogSearchMsg> msg;
        msg.header.id = LogSystem::LogSearchMsg::Worker_Hello;

        LogSystem::HelloMessage payload;
        payload.threads_available = threads;
        msg << payload;

        Send(msg);
    }

    void SendTaskDone(const LogSystem::TaskPayload& task) {
        uint64_t totalMatches{0};
        uint32_t linesFound{0};
        
        olc::net::message<LogSystem::LogSearchMsg> msg;
        msg.header.id = LogSystem::LogSearchMsg::Worker_TaskDone;
        msg << linesFound << totalMatches << task.search_id << task.task_id;

        Send(msg);
    }
    
    std::optional<LogSystem::TaskPayload> ReceiveTask(const std::chrono::milliseconds timeout) {
        olc::net::message<LogSystem::LogSearchMsg> msg;
        
        // Read header
        std::optional<std::error_code> result;
        asio::async_read(m_socket, asio::buffer(&msg.header, sizeof(msg.header)), 
        [&result](std::error_code ec, size_t) {
            result = ec;
        });


        m_context.restart();
        m_context.run_for(timeout);
        
        if (!result || *result || msg.header.id != LogSystem::LogSearchMsg::Server_SearchTask) {
            m_socket.cancel();
            m_context.restart();
            m_context.run(); // Run operation_aborted handler
            return std::nullopt;
        }
        
        msg.body.resize(msg.header.size);
        
        // Read body
        asio::read(m_socket, asio::buffer(msg.body.data(), msg.header.size));
        
        LogSystem::TaskPayload payload;
        msg >> payload;

        return payload;
    }

    void Crash() {
        m_socket.close();
    }

private:
    void Send(const olc::net::message<LogSystem::LogSearchMsg>& msg) {
        // Write header
        asio::write(m_socket, asio::buffer(&msg.header, sizeof(msg.header)));
    
        // Write body
        asio::write(m_socket, asio::buffer(msg.body.data(), msg.body.size()));
    }
private:
    asio::io_context m_context;
    asio::ip::tcp::socket m_socket;
};

#endif // FAKE_WORKER_HPP