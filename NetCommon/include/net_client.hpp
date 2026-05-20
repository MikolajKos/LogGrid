#ifndef NET_CLIENT_HPP
#define NET_CLIENT_HPP

#include "net_common.hpp"
#include "net_message.hpp"
#include "net_tsqueue.hpp"
#include "net_connection.hpp"

#include <thread>

namespace olc 
{
    namespace net
    {
        template <typename T>
        class client_interface {
        public:
            client_interface() : m_socket(m_context){}

            virtual ~client_interface() {
                Disconnect();
            };

        public:
            void Send(const message<T>& msg) {
                if (IsConnected()) {
                    m_connection->Send(msg);
                }
            }

            bool Connect(const std::string& host, const uint16_t port) {
                try {

                    // Resolve hosname/ip-address into tangiable physical address
                    asio::ip::tcp::resolver resolver(m_context);
                    auto endpoints = resolver.resolve(host, std::to_string(port));

                    // Create connection
                    m_connection = std::make_unique<connection<T>>(
                        connection<T>::owner::client,
                        m_context,
                        asio::ip::tcp::socket(m_context),
                        m_qMessagesIn
                    );
                    
                    m_connection->SetMsgCallback([this](olc::net::message<T>& msg) {
                        this->OnMessage(msg);
                    });
                    
                    // Tell the connection object to connect to a server
                    m_connection->ConnectToServer(endpoints, [this](bool bResult) {
                        this->OnConnectionResult(bResult);
                    });

                    // Start Context Thread
                    thrContext = std::thread([this]() { m_context.run(); });
                }
                catch (const std::exception& ex) {
                    std::cerr << "Client Exception: " << ex.what() << "\n";
                    return false;
                }
                
                return true;
            }

            void Disconnect() {
                if (IsConnected()) {
                    m_connection->Disconnect();
                }

                m_context.stop();

                if(thrContext.joinable())
                    thrContext.join();

                m_connection.release();
            }

            bool IsConnected() {
                if (m_connection)
                    return m_connection->IsConnected();
                else
                    return false;
            }

            // Retrieve queue of messages from server
            tsqueue<owned_message<T>>& Incomming() {
                return m_qMessagesIn;
            }
            
            
            
        protected:
            virtual void OnConnectionResult(bool bConnected) {};

            virtual void OnMessage(olc::net::message<T>& msg) {};

        protected:
            asio::io_context m_context;

            std::thread thrContext;
            
            asio::ip::tcp::socket m_socket;
            
            std::unique_ptr<connection<T>> m_connection;
            
        private:
            tsqueue<owned_message<T>> m_qMessagesIn;
        };
    }
}

#endif // NET_CLIENT_HPP