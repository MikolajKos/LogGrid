#ifndef MASTER_SERVER_HPP
#define MASTER_SERVER_HPP

#include <deque>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <future>

#include "olc_net.hpp"
#include "LogSearchCommon.hpp"

struct SearchSession {
    std::promise<LogSystem::SearchResult> promise;
    LogSystem::SearchResult result;
    int chunks_total = 0;
    int chunks_done = 0;
};

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

    std::future<LogSystem::SearchResult> StartSearch(const std::string& filepath, const std::string& keyword);

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

private:
    /**
     * @brief Helper method to dispatch the next available task to a specific client.
     * 
     * If the pending queue is empty, it sends a Server_JobFinished message.
     * 
     * @param client Shared pointer to the client requesting work.
     */
    bool DispatchNextTask(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client);

private:
    std::mutex m_stateMutex;
    std::deque<LogSystem::TaskPayload> m_pendingTasks;

    /** 
     * @brief Map tracking tasks currently being processed.
     * Key is the client ID, Value is the map of assigned tasks for each worker.
     */
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, LogSystem::TaskPayload>> m_inFlightTasks;

    /** 
     * @brief Workers waiting for job to be given.
     * If there are no pending tasks but some workers are still working. 
     */
    std::queue<std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>>> m_idleWorkers;

    std::unordered_map<uint64_t, SearchSession> m_sessions;
    
    std::unordered_map<uint32_t, uint64_t> m_workersFreeSlots;
    
    uint64_t m_nextSearchId = 0;
    uint64_t m_nextTaskId = 0;
};

#endif // MASTER_SERVER_HPP
