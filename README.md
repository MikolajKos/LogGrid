<div align="center">
  <img src="assets/loggrid-banner.png" alt="LogGrid - Distributed Asynchronous Log System">
</div>

# LogGrid
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=fff)](https://en.cppreference.com/w/cpp/20)
[![Docker](https://img.shields.io/badge/Docker-2496ED?logo=docker&logoColor=fff)](https://www.docker.com/)
[![CMake](https://img.shields.io/badge/CMake-064F8C?logo=cmake&logoColor=fff)](https://cmake.org/)
[![Asio](https://img.shields.io/badge/Asio-Network_Library-00599C?logo=c%2B%2B&logoColor=fff)](https://think-async.com/Asio/)
[![Google Test](https://img.shields.io/badge/gtest-Testing-23a71b?logo=c%2B%2B&logoColor=fff)](https://github.com/google/googletest)
[![Doxygen](https://img.shields.io/badge/Doxygen-Documentation-4A86CF?logo=readthedocs&logoColor=fff)](https://www.doxygen.nl/)
[![Status: WIP](https://img.shields.io/badge/Status-WIP%20(it's%20alive)-orange)]()

**LogGrid** is a distributed batch processing engine inspired by **MapReduce** and **Splunk** architectures. It splits gigabytes of telemetry files into byte-aligned chunks, processes them in parallel on stateless compute nodes and merges the partial results, triggered and queried through an asynchronous REST HTTP API. The current workload computes `count`, `sum`, `min`, `max` and `mean` of one telemetry channel in CSV format.

---

## How It Works

A single search request triggers a full distributed pipeline:

```
Client (curl / browser)
        │
        │ HTTP REST API (Async Polling)
        ▼
┌─────────────────────┐
│    Master Node      │  ← splits files into byte-aligned chunks
│  (HttpApi layer)    │    dispatches tasks, merges partial results
└──────┬──────────────┘
       │ TCP (binary protocol)
   ┌───┴───┐
   ▼       ▼
Worker   Worker        ← each Worker runs a local ThreadPool
   │       │             processes chunks in parallel
   └───┬───┘
       │
  [EFS / S3]           ← shared log file storage (cloud deployment)
```

1. Client sends `POST /api/search` with a file or directory path
2. Master splits the files into byte-aligned chunks and dispatches them across all connected Workers
3. Each Worker runs a local `ThreadPool`, parses the lines of its assigned chunk and computes `count`, `sum`, `min` and `max` of the `value` column
4. The chunk statistics go back to Master over TCP as one fixed-size `ChunkResult`
5. Master merges them into the session statistics (`TelemetryStats::Merge`)
6. Client polls `GET /api/status/{id}` for progress (`chunksDone` vs `chunksTotal`) and the merged statistics

---

## Architecture & Highlights

* **Master-Worker Pipeline** — Master splits log files into byte-aligned chunks and saturates every Worker thread with parallel tasks on connect. As each chunk completes, it is immediately replenished — eliminating idle gaps and maximising throughput.
* **Mergeable Statistics** — Workers return per-chunk `count`, `sum`, `min` and `max` as fixed-point integers (value × 10¹⁰), and the Master merges them with the accumulator model (`Add`/`Merge`, as in Flink's `AggregateFunction`). Integer addition does not depend on order, so the result does not depend on which chunk finishes first or which Worker processed it. A sum that does not fit in `int64_t` is reported with `overflow: true`.
* **Fault Tolerance** — On Worker disconnect, all in-flight tasks are atomically reclaimed and redistributed across every available idle Worker in a single pass — zero manual intervention, zero lost work. Verified on a Docker Compose cluster: in 73 runs a Worker was killed with `kill -9` at 5–95% of the work, and every session finished with all chunks done and a match count equal to `grep -c` (see [Benchmarks](#benchmarks)). These runs used the earlier regex search; the benchmark has not been moved to the statistics yet.
* **Async, Non-Blocking API** — Built on standalone ASIO (`io_context`). The legacy blocking `promise/future` patterns were removed. The REST API is fully asynchronous, allowing clients to poll live search states.
* **Stateless Workers, Persistent Connections** — Workers advertise their thread capacity on connect and remain alive between search sessions — ready to serve the next request without reconnection overhead.
* **Fully Containerized** — One `docker compose up --build` spins up the entire distributed cluster. Multi-stage Docker builds in Release mode, a shared log volume, and one scalable Worker service (`--scale worker=N`).

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Language | C++20 / C++23 |
| Networking (internal) | Standalone ASIO — async TCP, `io_context` |
| HTTP API | [cpp-httplib](https://github.com/yhirose/cpp-httplib) (header-only) |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) (header-only) |
| Concurrency | `std::mutex`, `std::jthread`, custom `ThreadPool` |
| Testing | GoogleTest — unit, parametrized, integration (real TCP) |
| Coverage | gcov / lcov — HTML report via CI artifact |
| Build | CMake + Ninja |
| Infrastructure | Docker, Docker Compose, Linux (Ubuntu base images) |

---

## Quick Start

### 1. Prepare Telemetry Data
LogGrid reads CSV files mounted into the cluster from `/data/logs`. Each line is `id,timestamp,channel,value` with no header, for example `1,946684823847,channel_60,0.7188446`. Values may have at most 10 digits after the dot (`LogSystem::kValueScaleDigits`); other lines are counted in `errors`.

* **Option A (Real data):** Convert a channel of the ESA-ADB dataset with `tools/generator/gen.py` (see [tools/generator/README.md](tools/generator/README.md)) and put the output in `/data/logs`.
* **Option B (Quick test):** Create a small sample file on your host:
  ```bash
  sudo mkdir -p /data/logs && printf '1,946684823847,channel_60,0.7188446\n2,946684853847,channel_60,0.7197575\n3,946684883847,channel_60,0.7307469\n' | sudo tee /data/logs/sample.csv
  ```

### 2. Spin up the LogGrid Cluster
```bash
# 1. Clone the repository
git clone https://github.com/MikolajKos/LogGrid.git && cd LogGrid

# 2. Spin up Master + Workers (2 by default; e.g. --scale worker=4 for more)
docker compose up -d --build --scale worker=2

# Optional: 2 CPUs and 2 threads per Worker. Without LOGGRID_WORKER_THREADS a Worker
# uses its machine's logical CPUs - 1 and does not see the CPU limit
# LOGGRID_WORKER_CPUS=2 LOGGRID_WORKER_THREADS=2 docker compose up -d --build --scale worker=2

# 3. Start an analysis (single file or entire directory)
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"path": "sample.csv"}'
# → {"searchId": 0}

# To analyze an entire directory recursively:
# -d '{"path": ""}'

# 4. Poll for results
curl -s http://localhost:8080/api/status/0 | python3 -m json.tool
# → "state": "Done", "stats": {"count": 3, "errors": 0, "failedChunks": 0, "sum": 21693490000, "scale": 10000000000,
#   "min": 7188446000, "max": 7307469000, "mean": 0.72311..., "overflow": false}

# 5. View live cluster logs
docker compose logs -f
```

---

## Testing

```bash
# Unit and integration tests
cmake -B build -G Ninja && cmake --build build
cd build && ctest --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -B build-asan -G Ninja -DLOGGRID_SANITIZER=address

# ThreadSanitizer; on Linux 6.x run build and tests with ASLR disabled
cmake -B build-tsan -G Ninja -DLOGGRID_SANITIZER=thread
setarch $(uname -m) -R cmake --build build-tsan
```

CI runs the test suites in the Release Docker image and in separate ASan/UBSan and TSan builds.

## Benchmarks

> [!WARNING]
> `bench/kill_worker.py` was written for the earlier regex search: it still sends `keyword` and compares `totalMatches` with `grep -c`, so it does not work with the statistics API. Moving it to the statistics and the reference program is tracked in `KNOWN_ISSUES.md`. The description below is for that version.

`bench/kill_worker.py` checks task recovery on the Docker Compose cluster. It starts the cluster, runs a search, kills Workers with SIGKILL once a given fraction of chunks is done, and compares the result with `grep -c` on the same files. Each run is PASS, FAIL (wrong count, hung session or a master failure after the search started), INVALID (the kill did not test recovery) or ERROR (the cluster did not start), and is saved as one CSV row in `bench/results/`.

```bash
# Run without a failure, then kills at 5/25/50/75/95% of chunks, 5 times
python3 bench/kill_worker.py --kill-at 0.05 0.25 0.5 0.75 0.95 --repeat 5

# 4 Workers limited to 2 CPUs each, kill 2 of them halfway
python3 bench/kill_worker.py --workers 4 --worker-cpus 2 --kill-count 2 --kill-at 0.5

# 2 CPUs and 4 threads per Worker, runs without a failure only
python3 bench/kill_worker.py --worker-cpus 2 --worker-threads 4 --kill-at
```

With `--worker-cpus`, the script sets the Worker thread count to the CPU limit rounded up; `--worker-threads N` sets another value. Both are saved in every CSV row.

Run it from the repository root. Test data comes from [CrazyPrinter API](https://github.com/MikolajKos/crazy-printer-api) and is generated on first use if the dataset directory does not exist. These runs check correctness on a single machine; they are not performance benchmarks.

---

## HTTP API Reference

### `POST /api/search` — Start a search

**Request body:**
```json
{
  "path": "sample.csv",
  "outputDir": "sessions/channel_60"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | ✅ | Target CSV file or directory (resolved relative to `LOGGRID_INPUT_DIR`, default: `/app/data`) |
| `outputDir` | string | ❌ | Subdirectory for the session result file (relative to `LOGGRID_OUTPUT_DIR`, default: `/data/loggrid`). The Master creates the directory, but no result file is written yet; the result is available only through `GET /api/status/{id}` |

> [!NOTE]
> **Path Resolution & Sandbox Security:**
> - `path` accepts both individual files (e.g. `"sample.csv"`) and whole directories (e.g. `"app_logs"` or `""` for the input root).
> - All paths are resolved relative to `LOGGRID_INPUT_DIR` (configured in `docker-compose.yml` or environment).
> - Paths are resolved with `std::filesystem::canonical`, including every symlink, and must stay inside the sandbox. Workers receive the resolved path, so the input directory must be mounted at the same location on the Master and all Workers.
> - A symlink named directly in `path` is accepted if its target is inside the sandbox.
> - When a directory is provided, the Master recursively scans all nested regular files, skips empty files and symlinks, and counts entries it cannot examine in `scanErrors` without aborting the search.

**Response `202 Accepted`:**
```json
{ "searchId": 0 }
```

**Response `404 Not Found`:** returned when `path` does not exist or resolves outside the sandbox, when a requested file cannot be processed (for example, its path is too long), or when a directory scan produced no tasks and hit at least one error.

---

### `GET /api/status/{id}` — Poll search status

**Response `200 OK`** (`ch60.csv` from ESA-ADB, 637 MB; while `"state"` is `"Running"`, `stats` holds the chunks merged so far):
```json
{
  "searchId": 0,
  "state": "Done",
  "chunksDone": 122,
  "chunksTotal": 122,
  "scanErrors": 0,
  "stats": {
    "count": 14639410,
    "errors": 0,
    "failedChunks": 0,
    "sum": 105473404436896030,
    "scale": 10000000000,
    "min": 108278130,
    "max": 9382211000,
    "mean": 0.7204757871860685,
    "overflow": false
  }
}
```

| `stats` field | Description |
|-------|-------------|
| `count` | Number of values parsed |
| `errors` | Number of lines the parser rejected, including a CSV header and empty lines |
| `failedChunks` | Number of chunks whose file a Worker could not open; their lines are not in `count` or `errors` |
| `sum`, `min`, `max` | Values scaled by `scale` (integers, for exact comparison): `min = 108278130` means `0.010827813` |
| `scale` | `10^kValueScaleDigits`, currently 10¹⁰ |
| `mean` | `sum / count / scale` as a floating-point number |
| `overflow` | `true` when the sum did not fit in `int64_t`; `sum` and `mean` are then left out |

`min`, `max` and `mean` are present only when `count > 0`; `sum` and `mean` are left out when `overflow` is `true`.

`sum`, `min` and `max` are 64-bit integers and `sum` is usually above 2^53, so read them with a JSON parser that keeps 64-bit integer precision: Python's `json` does, JavaScript's `JSON.parse` and parsers that store every number as a double do not, and round the last digits.

> [!IMPORTANT]
> `scanErrors` counts files and directories the Master could not examine while scanning. `"state": "Done"` with `scanErrors` above 0 means the search covered only part of the requested path. Likewise, `stats.failedChunks` above 0 means that a Worker could not open the file of some chunks (for example a missing volume mount, a file removed after the scan, or missing read permission), so the statistics cover only part of the data.

**Response `404 Not Found`:** returned when the `id` does not correspond to any known search session.

---

## Status

> **Work in Progress** — core networking, pipeline dispatch, fault tolerance, telemetry statistics, HTTP API, and testing infrastructure are functional.

### Done
- [x] **Async TCP networking** — non-blocking ASIO `io_context`; Master and Workers communicate via a typed binary protocol with fixed-size task and result structs
- [x] **Pipeline dispatch & result merging** — Master saturates Worker threads and merges the per-chunk statistics of each session in memory
- [x] **Telemetry statistics** — `count`, `sum`, `min`, `max` and `mean` of the `value` column, with fixed-point `int64_t` sums (value × 10¹⁰), overflow detection and an `errors` counter for rejected lines
- [x] **Fault tolerance** — all in-flight tasks atomically reclaimed on Worker disconnect and redistributed across every idle Worker in a single pass; verified with `kill -9` on a Docker Compose cluster
- [x] **Byte-aligned chunk splitting** — file partitioned at exact line boundaries; Worker ThreadPool processes chunks in parallel with CPU-oversubscription protection
- [x] **HTTP REST API** — `POST /api/search` triggers distributed search; `GET /api/status/{id}` asynchronously polls session progress (`chunksDone`) and the merged `stats`
- [x] **Recursive Directory Search** — `path` accepts both individual files and directories; Master enumerates all files recursively, skips empty files, and distributes chunks across a single unified search session
- [x] **Configurable Path Sandboxing** — `LOGGRID_INPUT_DIR` and `LOGGRID_OUTPUT_DIR` environment variables configure host/container paths with built-in Path Traversal protection
- [x] **Comprehensive CI/CD & testing** — GoogleTest unit and integration suites (parametrized, edge-case, and real TCP end-to-end); gcov/lcov coverage pipeline; ASan/UBSan and TSan builds in CI
- [x] **Containerization** — unified multi-stage `Dockerfile` with Release builds, Docker Compose with a scalable Worker service and optional CPU limit, port `8080` exposed for HTTP API
- [x] **Recovery benchmark** — `bench/kill_worker.py` kills Workers during a search and checks the result against `grep -c` (regex version; not moved to the statistics yet)

### Planned
- [ ] **Cloud deployment** — Master and Workers on AWS EC2; shared log storage on EFS/S3; Worker nodes auto-registered on boot
- [ ] **Plugin Architecture** — refactoring payload parsing to support dynamic analysis modules (WASM/Lua) beyond the built-in telemetry statistics

---

## Project Structure

```
.
├── CMakeLists.txt
├── bench
│   ├── kill_worker.py               # kill -9 recovery check on the Compose cluster
│   └── results/                     # CSV results and reports
├── docker-compose.yml
├── Dockerfile
├── NetClient                        # Worker node
│   └── src
│       ├── FileProcessor.hpp        # Chunk processing: reads lines, updates chunk stats
│       ├── TelemetryParser.hpp      # CSV line → fixed-point value (value × 10^10)
│       ├── ThreadPool.hpp           # Custom fixed-size thread pool
│       ├── WorkerClient.hpp/cpp     # ASIO client, task dispatch loop
│       └── main.cpp
├── NetCommon                        # Shared types and networking primitives
│   └── include
│       ├── ISearchService.hpp       # Service interface (SearchConfig, SearchStatus)
│       ├── LogSearchCommon.hpp      # Binary protocol structs (TaskPayload, ChunkResult), kValueScaleDigits
│       ├── TelemetryStats.hpp       # Mergeable statistics: Add, AddError, Merge
│       ├── net_client.hpp
│       ├── net_common.hpp
│       ├── net_connection.hpp
│       ├── net_message.hpp
│       ├── net_server.hpp
│       ├── net_tsqueue.hpp
│       └── olc_net.hpp              # ASIO networking layer (connection, server, client, tsqueue)
├── NetServer                        # Master node
│   └── src
│       ├── HttpApi.hpp/cpp          # HTTP transport — route registration, jthread lifecycle
│       ├── SearchController.hpp/cpp # Adapter — HTTP ↔ ISearchService, session map
│       ├── MasterServer.hpp/cpp     # Domain logic — dispatch, fault tolerance, aggregation
│       └── main.cpp
├── tests
│   ├── test_file_processor.cpp      # Unit tests — chunk boundaries, stats per chunk
│   ├── test_telemetry_parser.cpp    # Unit tests — value parsing, field count, overflow
│   ├── test_telemetry_stats.cpp     # Unit tests — Add, Merge, overflow
│   ├── test_thread_pool.cpp         # Unit tests — parametrized, FIFO ordering
│   ├── test_worker_client.cpp       # Unit tests — thread count edge cases
│   ├── test_worker_integration.cpp  # Integration tests — real TCP end-to-end
│   ├── test_master_integration.cpp  # Integration tests — Master dispatch and recovery
│   ├── FakeWorker.hpp               # Scripted Worker for Master tests
│   └── TestServer.hpp               # Minimal Master for Worker tests
├── tools
│   └── generator                    # ESA-ADB channel → telemetry CSV (gen.py)
└── external
    ├── asio/                        # Standalone ASIO
    ├── httplib.h                    # cpp-httplib (header-only)
    └── json.hpp                     # nlohmann/json (header-only)
```

---

## Acknowledgments

The foundational networking layer (socket management, asynchronous thread-safe queues) is based on the `olc::net` architecture by [javidx9 (OneLoneCoder)](https://github.com/OneLoneCoder). LogGrid extends this core with a custom application protocol, REST HTTP API, robust fault tolerance, and multi-threaded data processing.

_Copyright (c) 2026 Mikołaj Kosiorek. All rights reserved_
