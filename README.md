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
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)

**LogGrid** is a fast, distributed log analysis and search system inspired by **MapReduce** and **Splunk** architectures. It is designed for parallel processing of gigabytes of log files distributed across multiple stateless compute nodes, triggered and queried through a REST HTTP API.

---

## How It Works

A single search request triggers a full distributed pipeline:

```
Client (curl / browser)
        │
        │ HTTP REST API
        ▼
┌─────────────────────┐
│    Master Node      │  ← splits files into byte-aligned chunks
│  (HttpApi layer)    │    dispatches tasks, aggregates results
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

1. Client sends `POST /api/search` with a file path and keyword
2. Master splits the file into byte-aligned chunks and dispatches them across all connected Workers
3. Each Worker runs a local `ThreadPool` and searches its assigned chunk using regex
4. Matched lines stream back to Master over TCP
5. Client polls `GET /api/status/{id}` until the search completes

---

## Architecture & Highlights

* **Master-Worker Pipeline** — Master splits log files into byte-aligned chunks and saturates every Worker thread with parallel tasks on connect. As each chunk completes, it is immediately replenished — eliminating idle gaps and maximising throughput.
* **Fault Tolerance** — On Worker disconnect, all in-flight tasks are atomically reclaimed and redistributed across every available idle Worker in a single pass — zero manual intervention, zero lost work.
* **Async, Non-Blocking I/O** — Built on standalone ASIO (`io_context`). The Master is never blocked: message dispatch, result aggregation, and fault recovery all run through the same event loop without spinning or polling.
* **Stateless Workers, Persistent Connections** — Workers advertise their thread capacity on connect and remain alive between search sessions — ready to serve the next request without reconnection overhead.
* **HTTP API with clean layer separation** — `HttpApi` (transport) → `SearchController` (adapter) → `ISearchService` (interface) → `MasterServer` (domain). The search logic is fully decoupled from the HTTP layer and testable in isolation via mock injection.
* **Fully Containerized** — One `docker compose up --build` spins up the entire distributed cluster. Multi-stage Docker builds, a shared log volume, and a healthcheck guarantee correct startup ordering.

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Language | C++20 / C++23 |
| Networking (internal) | Standalone ASIO — async TCP, `io_context` |
| HTTP API | [cpp-httplib](https://github.com/yhirose/cpp-httplib) (header-only) |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) (header-only) |
| Concurrency | `std::mutex`, `std::jthread`, `std::promise/future`, custom `ThreadPool` |
| Testing | GoogleTest — unit, parametrized, integration (real TCP) |
| Coverage | gcov / lcov — HTML report via CI artifact |
| Build | CMake + Ninja |
| Infrastructure | Docker, Docker Compose, Linux (Ubuntu base images) |

---

## Quick Start

Starting the entire distributed cluster takes only seconds:

```bash
# 1. Clone the repository
git clone https://github.com/MikolajKos/LogGrid.git && cd LogGrid

# 2. Spin up Master + Workers
docker compose up -d --build

# 3. Trigger a search
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"path": "/app/data/sample.log", "keyword": "ERROR"}'
# → {"searchId": 0}

# 4. Poll for results
curl http://localhost:8080/api/status/0
# → {"linesFound": 42, "searchId": 0, "state": "Done"}

# 5. View live cluster logs
docker compose logs -f
```

---

## HTTP API Reference

### `POST /api/search` — Start a search

**Request body:**
```json
{
  "path": "/app/data/sample.log",
  "keyword": "ERROR|WARN",
  "maxResults": 10000
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `path` | string | ✅ | Absolute path to the log file on the shared volume |
| `keyword` | string | ✅ | Search term or regex pattern |
| `maxResults` | number | ❌ | Maximum matched lines to collect (default: 10 000) |

**Response `202 Accepted`:**
```json
{ "searchId": 0 }
```

---

### `GET /api/status/{id}` — Poll search status

**Response `200 OK` (search in progress):**
```json
{
  "searchId": 0,
  "state": "Running",
  "chunksDone": 14,
  "chunksTotal": 200,
  "linesFound": 382
}
```

**Response `200 OK` (search complete):**
```json
{
  "searchId": 0,
  "state": "Done",
  "linesFound": 42
}
```

**Response `404 Not Found`:** returned when the `id` does not correspond to any known search session.

---

## Status

> **Work in Progress** — core networking, pipeline dispatch, fault tolerance, HTTP API, and testing infrastructure are functional.

### Done
- [x] **Async TCP networking** — non-blocking ASIO `io_context`; Master and Workers communicate via a typed binary protocol with length-prefixed messages
- [x] **Pipeline dispatch & result aggregation** — Master saturates all Worker threads on connect, replenishes per-task ACK, and delivers aggregated results via `promise/future` per search session
- [x] **Fault tolerance** — all in-flight tasks atomically reclaimed on Worker disconnect and redistributed across every idle Worker in a single pass
- [x] **Byte-aligned chunk splitting** — file partitioned at exact line boundaries; Worker ThreadPool processes chunks in parallel with CPU-oversubscription protection
- [x] **HTTP REST API** — `POST /api/search` triggers distributed search; `GET /api/status/{id}` polls session progress; `ISearchService` interface decouples HTTP layer from domain logic
- [x] **Comprehensive CI/CD & testing** — GoogleTest unit and integration suites (parametrized, edge-case, and real TCP end-to-end); gcov/lcov coverage pipeline with HTML artifact; `FileProcessor` at 100% line coverage
- [x] **Production-grade containerization** — unified multi-stage `Dockerfile`, Docker Compose orchestration, healthcheck-enforced startup ordering, port `8080` exposed for HTTP API

### Planned
- [ ] **Result file & pagination** — Master writes matched lines to a per-session file on disk; `GET /api/search/{id}/results?offset=0&count=100` serves paginated lines; unbounded in-memory vector eliminated
- [ ] **Directory search** — `path` accepts a directory; Master enumerates all files and distributes chunks across a single search session; result lines tagged with source filename
- [ ] **`total_matches` counter** — track total number of matching lines independently of `maxResults` limit; surface in status response alongside `linesFound`
- [ ] **Cloud deployment** — Master and Workers on AWS EC2; shared log storage on EFS/S3; Worker nodes auto-registered on boot

---

## Project Structure

```
.
├── CMakeLists.txt
├── docker-compose.yml
├── Dockerfile
├── NetClient                        # Worker node
│   └── src
│       ├── FileProcessor.hpp        # Regex chunk search, line-by-line
│       ├── ThreadPool.hpp           # Custom fixed-size thread pool
│       ├── WorkerClient.hpp/cpp     # ASIO client, task dispatch loop
│       └── main.cpp
├── NetCommon                        # Shared types and networking primitives
│   └── include
│       ├── ISearchService.hpp       # Service interface (SearchHandle, SearchConfig, SearchStatus)
│       ├── LogSearchCommon.hpp      # Binary protocol structs (TaskPayload, ResultPayload, …)
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
│   ├── test_file_processor.cpp      # Unit tests — 100% line coverage
│   ├── test_thread_pool.cpp         # Unit tests — parametrized, FIFO ordering
│   ├── test_worker_client.cpp       # Unit tests — thread count edge cases
│   └── test_worker_integration.cpp  # Integration tests — real TCP end-to-end
└── external
    ├── asio/                        # Standalone ASIO
    ├── httplib.h                    # cpp-httplib (header-only)
    └── json.hpp                     # nlohmann/json (header-only)
```

---

## Acknowledgments

The foundational networking layer (socket management, asynchronous thread-safe queues) is based on the `olc::net` architecture by [javidx9 (OneLoneCoder)](https://github.com/OneLoneCoder). LogGrid extends this core with a custom application protocol, REST HTTP API, robust fault tolerance, and multi-threaded data processing.
