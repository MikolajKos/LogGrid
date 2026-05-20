#ifndef MASTER_SERVER_HPP
#define MASTER_SERVER_HPP

#include <deque>
#include <unordered_map>
#include <mutex>
#include <memory>

#include "olc_net.hpp"
#include "LogSearchCommon.hpp"

/**
 * @class MasterServer
 * @brief Central coordinator for the LogGrid distributed search system.
 * 
 * This class inherits from olc::net::server_interface. It manages the queue of 
 * pending tasks (log file chunks), dispatches them to connected stateless Workers, 
 * and ensures Fault Tolerance by tracking in-flight tasks. If a Worker disconnects 
 * prematurely, its task is reclaimed and pushed back to the pending queue.
 */
class MasterServer : public olc::net::server_interface<LogSystem::LogSearchMsg> {
public:
    /**
     * @brief Constructs the MasterServer and binds it to a specific port.
     * @param port The network port to listen on.
     */
    explicit MasterServer(uint16_t port);

    virtual ~MasterServer() = default;

    /**
     * @brief Adds a new search task to the pending queue.
     * @param task The task payload defining the file chunk and search keyword.
     */
    void AddTask(const LogSystem::TaskPayload& task);

protected:
    /**
     * @brief Called automatically when a new client connects.
     * @param client Shared pointer to the newly connected client.
     * @return true to accept the connection, false to reject it.
     */
    bool OnClientConnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) override;

    /**
     * @brief Called automatically when a client disconnects.
     * 
     * This is the core of the Fault Tolerance mechanism. It checks if the 
     * disconnected client had an active in-flight task and reclaims it if necessary.
     * 
     * @param client Shared pointer to the disconnected client.
     */
    void OnClientDisconnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) override;

    /**
     * @brief Event-driven callback triggered upon receiving a complete message.
     * @param client Shared pointer to the client that sent the message.
     * @param msg The received message packet.
     */
    void OnMessage(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client, 
                   olc::net::message<LogSystem::LogSearchMsg>& msg) override;
};

#endif // MASTER_SERVER_HPP