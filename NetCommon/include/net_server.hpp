#ifndef NET_SERVER_HPP
#define NET_SERVER_HPP

#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>

#include "net_common.hpp"
#include "net_connection.hpp"
#include "net_message.hpp"
#include "net_tsqueue.hpp"

namespace olc 
{
    namespace net
    {
        template <typename T>
        class server_interface {
        public:
            server_interface(uint16_t port) 
                : m_asioAcceptor(m_asioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {

            }

            virtual ~server_interface() {
                Stop();
            }

            bool Start() {
                try {
                    // Calls async function so when context starts it wont end its work immediately
                    WaitForClientConnection();

                    // Create new thread for context
                    m_threadContext = std::thread([this]() { m_asioContext.run(); });
                }
                catch (const std::exception& ex) {
                    // Sth porhibited the server from listening
                    std::cerr << "[SERVER] Exception: " << ex.what() << "\n";
                    return false;
                }

                std::cout << "[SERVER] Started!\n";
                return true;
            }

            void Stop() {
                m_asioContext.stop();

                if (m_threadContext.joinable())
                    m_threadContext.join();

                std::cout << "[SERVER] Stopped!\n";
            }

            // ASYNC - instruct asio to wait for connection
            void WaitForClientConnection() {
                m_asioAcceptor.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
                    if (!ec) {
                        std::cout << "[SERVER] New Connection: " << socket.remote_endpoint() << "\n";

                        std::shared_ptr<connection<T>> newconn =
                            std::make_shared<connection<T>>(connection<T>::owner::server,
                            m_asioContext, std::move(socket), m_qMessagesIn);

                        // Gives the user server a chance to deny connection
                        if (OnClientConnect(newconn)) {
                            // Connection allowed, so add to container of new connections
                            m_deqConnections.push_back(std::move(newconn));

                            m_deqConnections.back()->ConnectToClient(nIDCounter++);

                            std::cout << "[" << m_deqConnections.back()->GetID() << "] Connection Approved!\n";
                        }
                        else {
                            std::cout << "[SERVER] Connection Denied\n";
                        }
                    }
                    else {
                        std::cerr << "[SERVER] New Connection Error: " << ec.message() << "\n";
                    }

                    // Prime asio context with more work and wait for another connection
                    WaitForClientConnection();
                });
            }

            // Send a message to a specific client
            void MessageClient(std::shared_ptr<connection<T>> client, const message<T>& msg) {
                if (client && client->IsConnected()) {
                    client->Send(msg);
                }
                else {
                    OnClientDisconnect(client);
                    client.reset();
                    m_deqConnections.erase(
                        std::remove(m_deqConnections.begin(), m_deqConnections.end(), client), m_deqConnections.end());
                }
            }

            void MessageAllClients(const message<T>& msg, std::shared_ptr<connection<T>> pIgnoreClient = nullptr) {
                bool bInvalidClientExists = false;
                
                for (auto& client : m_deqConnections) {
                    if (client && client->IsConnected()) {
                        if (client != pIgnoreClient)
                            client->Send(msg);
                    }
                    else {
                        OnClientDisconnect(client);
                        client.reset();
                        bInvalidClientExists = true;
                    }
                }

                if (bInvalidClientExists)
                    m_deqConnections.erase(
                        std::remove(m_deqConnections.begin(), m_deqConnections.end(), nullptr), m_deqConnections.end());
            }

            void Update(size_t nMaxMessages = -1) {
                size_t nMessageCount = 0;

                while (nMessageCount < nMaxMessages && !m_qMessagesIn.empty()) {
                    // Grab the front message
                    auto msg = m_qMessagesIn.pop_front();

                    // Pass to message handler
                    OnMessage(msg.remote, msg.msg);

                    nMessageCount++;
                }
            }
            
        protected:
            // Called when a client connects, you can reject connection by returning false
            virtual bool OnClientConnect(std::shared_ptr<connection<T>> client) {
                return false;
            }

            // Called when a client appears to have disconnected
            virtual void OnClientDisconnect(std::shared_ptr<connection<T>> client) {
                
            }

            // Called when message arrives
            virtual void OnMessage(std::shared_ptr<connection<T>> client, message<T>& msg) {
                
            }

        protected:
            // Thread safe queue for incoming message packets
            tsqueue<owned_message<T>> m_qMessagesIn;

            std::deque<std::shared_ptr<connection<T>>> m_deqConnections;
            
            asio::io_context m_asioContext;

            std::thread m_threadContext;

            asio::ip::tcp::acceptor m_asioAcceptor;

            uint32_t nIDCounter = 10000;
        };
    }
}

#endif // NET_SERVER_HPP