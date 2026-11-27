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
 * Tracks the result file path, telemetry statistics merged from completed chunks,
 * and chunk completion progress.
 */
struct SearchSession {
    LogSystem::TelemetryStats stats{}; /**< Statistics merged from every completed chunk (TelemetryStats::Merge). */
    std::string path;          /**< Absolute result file path for this session. Its directory is created in BuildSession(); no file is written to it yet. */
    uint64_t search_id{0};     /**< Unique identifier assigned to this search session. */
    uint64_t scan_errors{0};   /**< Number of files or directories that could not be examined during the scan. */
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
     * @param config Session configuration options (output_dir for the result file path).
     * @return Unique search_id identifying the job.
     * @throws std::runtime_error If the input filepath does not exist, resolves outside the input
     *         directory, names a single file that cannot be processed, or names a directory whose
     *         scan produced no tasks and hit at least one error.
     */
    uint64_t StartSearch(const std::string& filepath, const SearchConfig& config) override;

    /**
     * @brief Retrieves real-time execution metrics and completion status for a given search session.
     * 
     * Thread-safe query invoked asynchronously by REST API controllers.
     * 
     * The statistics are a copy taken under m_stateMutex, merged from the chunks completed so far.
     * stats.errors counts every line the parser rejected, including a CSV header and empty lines.
     * When stats.count is 0, stats.min and stats.max keep their initial values (INT64_MAX and
     * INT64_MIN) and are not real values.
     *
     * @param search_id Unique identifier of the search session.
     * @return SearchStatus containing state (Running/Done), telemetry statistics, and chunk progress,
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
     * - Worker_TaskDone: Reads the ChunkResult and ignores it if its task_id is not in flight for
     *                    this Worker (late or duplicate result). Otherwise frees a worker slot,
     *                    dispatches the next task, merges the chunk statistics into the session,
     *                    counts the chunk as done and checks for job completion.
     *
     * A message too short for its expected fields is logged and discarded.
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
     * @brief Merges the statistics of one completed chunk into its session.
     *
     * Under m_stateMutex, calls TelemetryStats::Merge on the session found by
     * authoritativeSearchId. The search_id sent by the Worker is not used.
     *
     * @param batch Chunk result received from the Worker.
     * @param authoritativeSearchId Search ID taken from the m_inFlightTasks registry.
     * @throws std::runtime_error If no session has this search ID.
     */
    void AggregateTaskResult(const LogSystem::ChunkResult& batch, const uint64_t authoritativeSearchId);

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
     * @brief Reserves a unique, monotonically increasing search ID.
     *
     * Briefly locks m_stateMutex to increment m_nextSearchId. The ID is only reserved,
     * not published: no session is registered, so a failed search merely skips this ID.
     *
     * @return Unique searchId for a new session.
     */
    uint64_t AllocateSearchId();

    /**
     * @brief Builds a local, not-yet-published SearchSession object.
     *
     * Runs without holding m_stateMutex, because it performs filesystem I/O
     * (creates the output directory via CreateSessionFilePath()). chunks_total is left
     * at zero and is set later by AddPendingTasks().
     *
     * @param searchId ID obtained from AllocateSearchId().
     * @param config Search configuration (output_dir).
     * @return Session ready to be published with AddPendingTasks().
     * @throws std::runtime_error If a Path Traversal attempt is detected in output_dir.
     * @throws std::filesystem::filesystem_error If the output directory cannot be created.
     */
    SearchSession BuildSession(const uint64_t searchId, const SearchConfig& config);
    
    /**
     * @brief Constructs a baseline TaskPayload template for file chunking.
     * @param filepath Path of the target file.
     * @param searchId ID of the owning session.
     * @return TaskPayload with search_id and filename set; task_id and offsets are zero.
     */
    LogSystem::TaskPayload CreateChunkTask(const std::string& filepath, const uint64_t searchId);

    /**
     * @brief Slices a single file into byte-aligned chunks and appends them to a local task list.
     *
     * Computes start_offset and end_offset in increments of CHUNK_SIZE using std::min to
     * naturally handle the final incomplete chunk. Touches no shared state and takes no lock;
     * tasks are published later in a single step by AddPendingTasks().
     *
     * @param filepath Absolute/relative path to the file on disk.
     * @param fileSize Size of the current file to which path was passed
     * @param baseTask Template task containing search parameters.
     * @param outTasks Caller-owned list of tasks being built for the session; chunks are appended to it.
     */
    void EnqueueFileChunks(const std::filesystem::path& filepath, const uint64_t fileSize, LogSystem::TaskPayload baseTask, std::vector<LogSystem::TaskPayload>& outTasks);

    /**
     * @brief Wakes up idle Workers and assigns pending chunks up to their available thread count.
     * 
     * Iterates while both m_pendingTasks and m_idleWorkers are non-empty. Pops Workers one by one
     * and calls DispatchNextTask() for each of their free slots, stopping immediately if tasks run out.
     */
    void AssignIdleWorkers();

    /**
     * @brief Recursively scans a directory using standard iteration to ensure fault tolerance against missing files.
     * 
     * An entry the Master cannot examine (e.g. a file removed by log rotation or a directory without
     * read permission) does not terminate the global search; it is counted in scanErrors instead.
     * Symlinks are skipped deliberately and are not counted as errors. A file's contents are not
     * checked here: Workers open it later, so a file they cannot read is not counted.
     *
     * @param dir Directory to scan.
     * @param searchId The ID of the session.
     * @param outTasks Caller-owned list shared by all recursion levels; chunks of every file found are appended to it.
     * @param scanErrors Caller-owned counter shared by all recursion levels; incremented for each failure.
     */
    void ScanDirectoryRecursively(const std::filesystem::path& dir, uint64_t searchId, std::vector<LogSystem::TaskPayload>& outTasks, uint64_t& scanErrors);

    /**
     * @brief Reads file size, validates path length, and appends the file's chunks to a local task list.
     *
     * Extracted to prevent code duplication between the root file handling and recursive file traversal.
     * The caller decides how to react to a failure: a directory scan counts it and continues,
     * while a search targeting this file directly fails.
     *
     * @param targetPath The single regular file to process.
     * @param searchId The ID of the session.
     * @param outTasks Caller-owned list of tasks being built for the session; chunks are appended to it.
     * @return true if the file was handled, including an empty file that produces no chunks;
     *         false if its size cannot be read or its path is too long for TaskPayload::filename.
     */
    bool ProcessSingleFile(const std::filesystem::path& targetPath, uint64_t searchId, std::vector<LogSystem::TaskPayload>& outTasks);

    /**
     * @brief Atomically publishes a fully built session together with all of its tasks.
     *
     * Under a single m_stateMutex lock, sets session.chunks_total, inserts the session into
     * m_sessions and appends all tasks to m_pendingTasks. Other threads (GetStatus,
     * DispatchNextTask) therefore never observe a partially built session.
     *
     * The session is inserted before the tasks: if appending to the deque throws, the deque
     * is left unchanged and at most an unreachable session entry remains, rather than
     * orphaned tasks consuming Worker capacity.
     *
     * @param session Session built by BuildSession(); moved into m_sessions.
     * @param newTasks All chunk tasks collected during the scan phase.
     */
    void AddPendingTasks(SearchSession&& session, const std::vector<LogSystem::TaskPayload>& newTasks);
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

    std::string m_base_dir; /**< Root output storage sandbox directory for LogGrid session files (e.g. /data/loggrid). */
    std::string m_input_dir; /**< Root input directory where logs are stored */
};

#endif // MASTER_SERVER_HPP