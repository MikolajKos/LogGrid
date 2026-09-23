#ifndef MASTER_SERVER_HPP
#define MASTER_SERVER_HPP

#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "olc_net.hpp"
#include "ISearchService.hpp"
#include "LogSearchCommon.hpp"

/**
 * @struct SearchSession
 * @brief Represents the internal state and metrics of an active or completed search job.
 * 
 * Tracks the target result path, global statistics (lines found, total matches),
 * and chunk completion progress.
 */
struct SearchSession {
    std::string path;          /**< Absolute path to the aggregated output result log file on disk. */
    uint64_t search_id{0};     /**< Unique identifier assigned to this search session. */
    uint64_t total_matches{0}; /**< Total count of regex matches detected across all processed chunks. */
    uint32_t line_count{0};    /**< Number of matched lines written to disk (bounded by max_results). */
    int chunks_total = 0;      /**< Total number of byte-aligned chunks into which the file(s) were sliced. */
    int chunks_done = 0;       /**< Number of chunks completed and acknowledged by Workers. */
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
class MasterServer : public olc::net::server_interface<LogSystem::LogSearchMsg>,
                     public ISearchService {
public:
    /**
     * @brief Constructs the MasterServer and binds the internal ASIO TCP server to a port.
     * @param port The TCP port to listen on for Worker connections.
     */
    explicit MasterServer(uint16_t port);
    
    /**
     * @brief Virtual destructor to ensure polymorphic cleanup via ISearchService pointers.
     */
    virtual ~MasterServer() = default;

    /**
     * @brief Initiates a new distributed log search session.
     * 
     * Validates input path existence, registers a new SearchSession, partitions the file into
     * byte-aligned chunks (pushed to m_pendingTasks), and dispatches work to idle Workers.
     * 
     * @param filepath Path to the log file (or directory) to search.
     * @param keyword Regex search pattern or exact keyword.
     * @param config Session configuration options (max_results limit, output_dir).
     * @return Unique search_id identifying the job.
     * @throws std::runtime_error If the input filepath does not exist or if path traversal is detected.
     */
    uint64_t StartSearch(const std::string& filepath, const std::string& keyword, const SearchConfig& config) override;

    /**
     * @brief Retrieves real-time execution metrics and completion status for a given search session.
     * 
     * Thread-safe query invoked asynchronously by REST API controllers.
     * 
     * @param search_id Unique identifier of the search session.
     * @return SearchStatus containing state (Running/Done), metrics, and chunk progress,
     *         or std::nullopt if the session does not exist.
     */
    std::optional<SearchStatus> GetStatus(const uint64_t search_id) override;

protected:
    /**
     * @brief Called automatically when a new client connects.
     * @param client Shared pointer to the newly connected client.
     * @return true to accept the connection, false to reject it.
     */
    bool OnClientConnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) override;

    /**
     * @brief Event callback triggered when a Worker disconnects prematurely.
     * 
     * Core of the Fault Tolerance subsystem. Identifies all unacknowledged in-flight tasks
     * assigned to this Worker, atomically prepends them back to m_pendingTasks, and triggers
     * AssignIdleWorkers() to redistribute them to available healthy nodes.
     * 
     * @param client Shared pointer to the disconnected Worker connection.
     */
    void OnClientDisconnect(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client) override;

    /**
     * @brief Event callback triggered upon arrival of a complete TCP message from a Worker.
     * 
     * Handles:
     * - Worker_Hello: Registers Worker capacity and saturates all available threads with tasks.
     * - Worker_TaskDone: Aggregates result metrics, writes batch lines to disk, frees a worker slot,
     *                    checks for job completion, and immediately dispatches the next task.
     * 
     * @param client Shared pointer to the Worker that sent the message.
     * @param msg The binary message payload.
     */
    void OnMessage(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client, 
                   olc::net::message<LogSystem::LogSearchMsg>& msg) override;

private:
    /**
     * @brief Dispatches the next available task from the pending queue to a specified Worker.
     * 
     * Thread-safe. If m_pendingTasks has items, pops a task, assigns it an incremental task_id,
     * updates in-flight tracking, decrements free slots, and sends it over TCP.
     * If no tasks remain, adds the Worker to m_idleWorkers (if not already queued).
     * 
     * @param client Target Worker to receive work.
     * @return true if a task was successfully sent; false if no tasks were available.
     */
    bool DispatchNextTask(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client);

    /**
     * @brief Processes a completed task payload received over TCP.
     * 
     * Extracts task_id and deserializes the ChunkResult batch. Under m_stateMutex, updates
     * cumulative line_count and total_matches. Subsequently invokes WriteResults outside lock.
     * 
     * @param msg Incoming message containing task_id and serialized batch.
     * @return The task_id that finished execution.
     * @throws std::runtime_error If search_id is unrecognized.
     */
    uint64_t AggregateTaskResult(olc::net::message<LogSystem::LogSearchMsg>& msg);

    /**
     * @brief Deserializes a binary batch payload into a ChunkResult structure.
     * 
     * Pops POD headers (search_id, total_matches, line_count) from the message tail,
     * then extracts length-prefixed matched lines from the front of the body.
     * 
     * @param msg Raw network packet received from Worker.
     * @return Structured ChunkResult populated with metrics and matched lines.
     */
    LogSystem::ChunkResult DeserializeBatch(olc::net::message<LogSystem::LogSearchMsg>& msg);

    /**
     * @brief Appends batched string lines to the session log file on disk.
     * 
     * Looks up file path under m_stateMutex, then opens the file in append mode (std::ios::app)
     * and flushes lines without holding locks to prevent disk I/O from stalling cluster networking.
     * 
     * @param lines Vector of matched log lines to write.
     * @param searchId The ID of the session whose file should be appended.
     */
    void WriteResults(std::vector<std::string>& lines, const uint64_t searchId);
    
    /**
     * @brief Opens or appends to a result file on disk.
     * @param filename Target file path.
     * @return Open std::ofstream stream in append binary mode, or std::nullopt on failure.
     */
    std::optional<std::ofstream> OpenResultFile(const std::string& filename);

    /**
     * @brief Generates a secure, timestamped result file path and creates parent directories.
     * 
     * Normalizes the combined path using lexically_normal() and prevents Path Traversal attacks
     * by verifying that the normalized target starts with m_base_dir.
     * 
     * @param userDir User-requested output directory relative to base dir.
     * @param searchId ID of the search session.
     * @return Absolute file path string for the session output.
     * @throws std::runtime_error If a Path Traversal attempt is detected.
     */
    std::string CreateSessionFilePath(const std::string& userDir, const uint64_t searchId);

    /**
     * @brief Strips leading directory separators from a path string view.
     * @param path The input path view.
     * @return Relative filesystem path.
     */
    std::filesystem::path MakeRelative(std::string_view path);

    /**
     * @brief Allocates a new searchId and registers a fresh SearchSession in m_sessions.
     * 
     * Performs directory creation outside lock, then briefly locks m_stateMutex to insert the session.
     * 
     * @param config Search configuration containing output directory.
     * @return Unique searchId allocated for the session.
     */
    uint64_t RegisterNewSession(const SearchConfig& config);
    
    /**
     * @brief Constructs a baseline TaskPayload template for file chunking.
     * @param filepath Path of the target file.
     * @param keyword Pattern/string to search for.
     * @param searchId ID of the owning session.
     * @param config Search configuration (contains max_results).
     * @return Populated TaskPayload with search metadata and zeroed offsets.
     */
    LogSystem::TaskPayload CreateChunkTask(const std::string& filepath, const std::string& keyword, const uint64_t searchId, const SearchConfig& config);

    /**
     * @brief Slices a single file into byte-aligned chunks and enqueues them for processing.
     * 
     * Computes start_offset and end_offset in increments of CHUNK_SIZE using std::min to 
     * naturally handle the final incomplete chunk. Pushes tasks to m_pendingTasks and
     * increments chunks_total in the session under m_stateMutex.
     * 
     * @param filepath Absolute/relative path to the file on disk.
     * @param baseTask Template task containing search parameters.
     */
    void EnqueueFileChunks(const std::filesystem::path& filepath, LogSystem::TaskPayload baseTask);

    /**
     * @brief Wakes up idle Workers and assigns pending chunks up to their available thread count.
     * 
     * Iterates while both m_pendingTasks and m_idleWorkers are non-empty. Pops Workers one by one
     * and calls DispatchNextTask() for each of their free slots, stopping immediately if tasks run out.
     */
    void AssignIdleWorkers();
private:
    std::mutex m_stateMutex; /**< Primary mutex protecting all shared scheduler and session state. */

    std::deque<LogSystem::TaskPayload> m_pendingTasks; /**< FIFO queue of chunks waiting to be assigned to Workers. */

    /** 
     * @brief Map tracking tasks currently in-flight across all active Workers.
     * 
     * Key: Worker client ID (uint32_t).
     * Value: Inner map mapping task_id (uint64_t) to the assigned TaskPayload.
     * Used for Fault Tolerance recovery upon node disconnect.
     */
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, LogSystem::TaskPayload>> m_inFlightTasks;

    /** 
     * @brief Queue of connected Workers that currently have zero assigned tasks and are awaiting work.
     */
    std::queue<std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>>> m_idleWorkers;

    /**
     * @brief Set of client IDs currently present in m_idleWorkers.
     *
     * Prevents the same Worker from being enqueued multiple times when
     * several of its threads complete tasks concurrently while the pending
     * queue is empty. Must be kept in sync with m_idleWorkers:
     * insert on push, on pop.
     */
    std::unordered_set<uint32_t> m_idleWorkersIds;
    
    std::unordered_map<uint64_t, SearchSession> m_sessions; /**< Active and completed search sessions indexed by searchId. */
    
    std::unordered_map<uint32_t, uint64_t> m_workersFreeSlots; /**< Number of available processing thread slots per Worker ID. */
    
    uint64_t m_nextSearchId = 0; /**< Monotonically increasing counter for assigning unique search IDs. */
    uint64_t m_nextTaskId = 0; /**< Monotonically increasing counter for assigning unique task IDs. */

    std::string m_base_dir; /**< Root storage sandbox directory for LogGrid session files (e.g. /data/loggrid). */
};

#endif // MASTER_SERVER_HPP