# LogGrid (Work in Progress)

**LogGrid** is a fast, distributed log analysis and search system inspired by **MapReduce** and **Splunk** architectures. It is designed for parallel processing of gigabytes of text files distributed across multiple stateless compute nodes. 

By leveraging modern C++ concurrency and a fully containerized ecosystem, LogGrid provides a blazing-fast, fault-tolerant infrastructure capable of ingesting and querying live log streams.

---

## Architecture

The system is built on an event-driven, push-based asynchronous TCP/IP architecture, orchestrated entirely via **Docker Compose**. It implements a Producer-Consumer pattern for log ingestion and consists of the following core components:

* **Master (Server):** The central coordinator. It manages the task queue, distributes file chunks, and aggregates search results. It features robust **Built-in Fault Tolerance** – if an assigned Worker crashes or disconnects, its in-flight task is immediately reclaimed and seamlessly redistributed to another active node.
* **Worker (Client):** A highly optimized, stateless compute node. It connects to the Master, requests work, and utilizes a custom `ThreadPool` (`std::thread::hardware_concurrency`) for parallel file parsing, streaming matching log lines back to the Master in real-time.
* **Log Generator / Shared Volume:** (Under Implementation) A dynamic ingestion layer where microservices write live logs to a shared Docker Volume, allowing Workers to process incoming data on the fly.

## Key Features

* **Infrastructure as Code (IaC):** Fully containerized using `Dockerfile` (Multi-stage builds) and `docker-compose.yml`. Starts the entire distributed cluster with a single command.
* **Environment Agnostic:** Eliminates OS-level dependencies (Windows/Linux conflicts) through isolated Ubuntu-based containers and environment variable configuration.
* **Zero-Downtime Reassignment:** Advanced TCP socket monitoring detects silent connection drops and instantly reassigns tasks.
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

## Acknowledgments

The foundational networking layer (socket management, asynchronous thread-safe queues) is based on the `olc::net` architecture by [javidx9 (OneLoneCoder)](https://github.com/OneLoneCoder). `LogGrid` extends this core with a custom application protocol, robust fault tolerance, and multi-threaded data processing.
