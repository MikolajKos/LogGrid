#ifndef NET_CONNECTION_HPP
#define NET_CONNECTION_HPP

#include <functional>

#include "net_common.hpp"
#include "net_message.hpp"
#include "net_tsqueue.hpp"

namespace olc 
{
    namespace net
    {
        template <typename T>
        class connection : public std::enable_shared_from_this<connection<T>> {
        public:
            enum class owner {
                server,
                client
            };

            connection(owner parent, asio::io_context& asioContext, asio::ip::tcp::socket socket, tsqueue<owned_message<T>>& qIn)
                : m_asioContext(asioContext), m_socket(std::move(socket)), m_qMessagesIn(qIn) {
                m_nOwnerType = parent;
            }

            virtual ~connection() {

            }

            uint32_t GetID() const {
                return id;
            }

        public:            
            void ConnectToClient(uint32_t uid = 0) {
                if (m_nOwnerType == owner::server) {
                    if (m_socket.is_open()) {
                        id = uid;
                        ReadHeader();
                    }
                }
            }
            
            void ConnectToServer(const asio::ip::tcp::resolver::results_type& endpoints,
                std::function<void(bool)> funcHandler) {
                asio::async_connect(m_socket, endpoints,
                    [this, funcHandler](std::error_code ec, asio::ip::tcp::endpoint endpoint) {
                        if (!ec) {
                            // Success
                            ReadHeader();
                            funcHandler(true);
                        }
                        else {
                            m_socket.close();
                            funcHandler(false);
                        }
                    });
            }

            void Disconnect() {
                if (IsConnected())
                    asio::post(m_asioContext, [this]() { m_socket.close(); });
            }

            bool IsConnected() const {
                return m_socket.is_open();
            }

            void Send(const message<T>& msg) {
                asio::post(m_asioContext, [this, msg]() {
                    bool bWrittingMessage = !m_qMessagesOut.empty();

                    m_qMessagesOut.push_back(msg);
                    
                    if (!bWrittingMessage)
                        WriteHeader();
                });
            }

            void SetMsgCallback(std::function<void(message<T>&)> callback) {
                m_msgCallback = callback;
            }
            
        private:
            void ReadHeader() {
                    asio::async_read(m_socket, asio::buffer(&m_msgTemporaryIn.header, sizeof(message_header<T>)),
                    [this](asio::error_code ec, size_t length) {
                        if (!ec) {
                            if (m_msgTemporaryIn.header.size > 0) {
                                m_msgTemporaryIn.body.resize(m_msgTemporaryIn.header.size);
                                ReadBody();
                            }
                            else { // Message without body
                                AddToIncomingMessageQueue();
                            }
                        }
                        else {
                            std::cout << "[" << id << "] Read Header Fail.\n";
                            m_socket.close();
                        }
                    });
            }

            void ReadBody() {
                asio::async_read(m_socket, asio::buffer(m_msgTemporaryIn.body.data(), m_msgTemporaryIn.body.size()),
                    [this](asio::error_code ec, size_t length) {
                        if (!ec) {
                            AddToIncomingMessageQueue();
                        }
                        else {
                            std::cout << "[" << id << "] Read Body Fail.\n";
                            m_socket.close();
                        }
                    });
            }

            void WriteHeader() {
                asio::async_write(m_socket, asio::buffer(&m_qMessagesOut.front().header, sizeof(message_header<T>)),
                    [this](asio::error_code ec, size_t length) {
                        if (!ec) {
                            if (m_qMessagesOut.front().body.size() > 0) {
                                WriteBody();
                            }
                            else {
                                m_qMessagesOut.pop_front();

                                if (!m_qMessagesOut.empty()) {
                                    WriteHeader();
                                }
                            }
                        }
                        else {
                            std::cout << "[" << id << "] Write Header Fail.\n";
                            m_socket.close();
                        }
                    });
            }

            void WriteBody() {
                asio::async_write(m_socket, asio::buffer(m_qMessagesOut.front().body.data(), m_qMessagesOut.front().body.size()),
                    [this](asio::error_code ec, size_t length) {
                        if (!ec) {
                            m_qMessagesOut.pop_front();

                            if (!m_qMessagesOut.empty()) {
                                WriteHeader();
                            }
                        }
                        else {
                            std::cout << "[" << id << "] Write Body Fail.\n";
                        }
                    });
            }

            void AddToIncomingMessageQueue() {
                if (m_nOwnerType ==  owner::server)
                    m_qMessagesIn.push_back({ this->shared_from_this(), m_msgTemporaryIn });
                else {
                    // If callback is set message is handled but not added to queue
                    if (m_msgCallback)
                        m_msgCallback(m_msgTemporaryIn);
                    else // Otherwise add to queue
                        m_qMessagesIn.push_back({ nullptr, m_msgTemporaryIn });
                }

                ReadHeader();
            }

            protected:
            asio::io_context& m_asioContext;
            
            // Each connection has a unique socket to a remote
            asio::ip::tcp::socket m_socket;

            // This queue holds all messages to be sent to the remote side
            // of this connection
            tsqueue<message<T>> m_qMessagesOut;

            // This queue holds all messages that have been recieved from
            // the remote side of this connection. Note it is a reference
            // as the "owner" of this connection is expected to provide a queue
            tsqueue<owned_message<T>>& m_qMessagesIn;
            message<T> m_msgTemporaryIn;

            // The ownership decides how some of the connections behaves
            owner m_nOwnerType = owner::server;
            uint32_t id = 0;

            // OnMessage callback foothold
            std::function<void(message<T>&)> m_msgCallback;
        };
    }
}

#endif // NET_CONNECTION_HPP