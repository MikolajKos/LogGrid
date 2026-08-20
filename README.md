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

**LogGrid** is a fast, distributed log analysis and search system inspired by **MapReduce** and **Splunk** architectures. It is designed for parallel processing of gigabytes of text files distributed across multiple stateless compute nodes. 

By leveraging modern C++ concurrency and a fully containerized ecosystem, LogGrid provides a blazing-fast, fault-tolerant infrastructure capable of ingesting and querying live log streams.

---

## Architecture

The system is built on an event-driven, push-based asynchronous TCP/IP architecture, orchestrated entirely via **Docker Compose**. It implements a Producer-Consumer pattern for log ingestion and consists of the following core components:

* **Master (Server):** The central coordinator. It manages the task queue, dispatches multiple tasks simultaneously to each Worker based on its thread count, and aggregates search results. It features robust **Built-in Fault Tolerance** – if a Worker crashes, all its in-flight tasks are immediately reclaimed and redistributed across all available idle Workers.
* **Worker (Client):** A highly optimized, stateless compute node. On connect it advertises its thread capacity via a `Worker_Hello` handshake. The Master immediately saturates all Worker threads with parallel tasks. Each completed task is acknowledged by `task_id`, allowing multiple tasks to be in-flight simultaneously without ambiguity.
* **Log Generator / Shared Volume:** A dynamic ingestion layer where microservices write live logs to a shared Docker Volume, allowing Workers to process incoming data on the fly. A Docker healthcheck ensures the Master starts only after the log file is fully generated.

## Key Features

* **Infrastructure as Code (IaC):** Fully containerized using `Dockerfile` (Multi-stage builds) and `docker-compose.yml`. Starts the entire distributed cluster with a single command.
* **Environment Agnostic:** Eliminates OS-level dependencies (Windows/Linux conflicts) through isolated Ubuntu-based containers and environment variable configuration.
* **Pipeline Dispatch:** Master saturates all Worker threads immediately — dispatches N tasks per Worker on connect and replenishes after each `Worker_TaskDone` ACK, eliminating idle gaps between task completions.
* **Zero-Downtime Reassignment:** When a Worker crashes, all its in-flight tasks are reclaimed at once and redistributed across every available idle Worker in a single pass.
* **High-Performance I/O:** Asynchronous network streaming ensures the Master node is never blocked, even with dozens of connected Workers.

## Tech Stack

* **Language:** C++20 / C++23
* **Networking:** Asynchronous I/O based on Standalone ASIO (`asio::io_context`).
* **Concurrency:** Modern C++ threading primitives (`std::mutex`, `std::condition_variable`, custom Thread Pool).
* **Infrastructure:** Docker, Docker Compose, Linux (Ubuntu base images).
* **Build System:** CMake + Ninja.

## Quick Start (Running the Cluster)

Starting the entire distributed architecture takes only seconds:

1. Clone the repository.
2. Ensure you have Docker and Docker Compose installed.
3. Spin up the Master and multiple Worker nodes using:
   ```bash
   docker compose up -d --build
   ```
4. To view the live, aggregated output of the cluster:
   ```bash
   docker compose logs -f
   ```

## Status

> **Work in Progress** — core networking, pipeline dispatch, fault tolerance, result aggregation and testing infrastructure are functional.

### Done
- [x] Async TCP networking layer (Asio `io_context`, non-blocking I/O)
- [x] Master-Worker task distribution protocol
- [x] Fault Tolerance — full reclaim of all in-flight tasks on Worker disconnect, redistributed across all idle Workers
- [x] Byte-aligned chunk splitting (correct line boundary detection)
- [x] Result aggregation — `promise/future` per search session, results delivered to caller
- [x] Worker ThreadPool — parallel chunk processing via `std::thread::hardware_concurrency()`
- [x] Docker healthcheck — Master waits for log file to be fully generated before starting
- [x] Pipeline dispatch — Master dispatches N tasks per Worker simultaneously based on advertised thread count (`Worker_Hello` handshake); free slots tracked dynamically via `m_workersFreeSlots`
- [x] Per-task ACK — `Worker_TaskDone` carries `task_id` for precise completion tracking with multiple in-flight tasks per Worker
- [x] Worker ThreadPool optimization — use `hardware_concurrency() - 1` threads to avoid starving the ASIO I/O thread
- [x] Unit tests — GoogleTest suite for `ThreadPool`, `FileProcessor`, `WorkerClient` with parametrized and edge-case coverage
- [x] Code coverage — gcov/lcov pipeline in CI, HTML report uploaded as artifact, `FileProcessor` at 100% line coverage
- [x] Integration tests — real TCP end-to-end tests: `Worker_Hello` handshake, task dispatch with `task_id` ACK verification, `Worker_FoundLine` content matching

### Planned
- [ ] HTTP API — Master exposes `/search` endpoint, replacing hardcoded `StartSearch` call
- [ ] Cloud deployment on AWS (EC2 instances as Worker nodes)

## Structure
```bash
.
├── CMakeLists.txt
├── docker-compose.yml
├── Dockerfile
├── NetClient
│   ├── CMakeLists.txt
│   └── src
│       ├── FileProcessor.hpp
│       ├── main.cpp
│       ├── ThreadPool.hpp
│       ├── WorkerClient.cpp
│       └── WorkerClient.hpp
├── NetCommon
│   ├── CMakeLists.txt
│   └── include
│       ├── LogSearchCommon.hpp
│       ├── net_client.hpp
│       ├── net_common.hpp
│       ├── net_connection.hpp
│       ├── net_message.hpp
│       ├── net_server.hpp
│       ├── net_tsqueue.hpp
│       └── olc_net.hpp
├── NetServer
│   ├── CMakeLists.txt
│   └── src
│       ├── main.cpp
│       ├── MasterServer.cpp
│       └── MasterServer.hpp
├── tests
│   ├── CMakeLists.txt
│   ├── TestServer.hpp
│   ├── test_file_processor.cpp
│   ├── test_thread_pool.cpp
│   ├── test_worker_client.cpp
│   └── test_worker_integration.cpp
└── README.md
```

## Acknowledgments

The foundational networking layer (socket management, asynchronous thread-safe queues) is based on the `olc::net` architecture by [javidx9 (OneLoneCoder)](https://github.com/OneLoneCoder). `LogGrid` extends this core with a custom application protocol, robust fault tolerance, and multi-threaded data processing.
