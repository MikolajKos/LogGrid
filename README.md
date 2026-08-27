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

## Architecture & Highlights

* **Master-Worker Pipeline:** The Master splits log files into byte-aligned chunks and saturates every Worker thread with parallel tasks on connect. As each chunk completes, it is immediately replenished — eliminating idle gaps and maximising throughput.
* **Fault Tolerance:** On Worker disconnect, all in-flight tasks are atomically reclaimed and redistributed across every available idle Worker in a single pass — zero manual intervention, zero lost work.
* **Async, Non-Blocking I/O:** Built on standalone ASIO (`io_context`). The Master is never blocked: message dispatch, result aggregation, and fault recovery all run through the same event loop without spinning or polling.
* **Stateless Workers, Persistent Connections:** Workers advertise their thread capacity on connect and remain alive between search sessions — ready for a future HTTP API without reconnection overhead.
* **Fully Containerized:** One `docker compose up --build` spins up the entire distributed cluster. Multi-stage Docker builds, a shared log volume, and a healthcheck guarantee correct startup ordering.

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
- [x] **Async TCP networking** — non-blocking ASIO `io_context`; Master and Workers communicate via a typed binary protocol with length-prefixed messages
- [x] **Pipeline dispatch & result aggregation** — Master saturates all Worker threads on connect, replenishes per-task ACK, and delivers aggregated results via `promise/future` per search session
- [x] **Fault tolerance** — all in-flight tasks atomically reclaimed on Worker disconnect and redistributed across every idle Worker in a single pass
- [x] **Byte-aligned chunk splitting** — file partitioned at exact line boundaries; Worker ThreadPool processes chunks in parallel with CPU-oversubscription protection
- [x] **Comprehensive CI/CD & testing** — GoogleTest unit and integration suites (parametrized, edge-case, and real TCP end-to-end scenarios); gcov/lcov coverage pipeline with HTML artifact; `FileProcessor` at 100% line coverage
- [x] **Production-grade containerization** — unified multi-stage `Dockerfile`, Docker Compose orchestration, and healthcheck-enforced startup ordering

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
