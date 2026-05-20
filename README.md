# LogGrid (Work in Progress)

**LogGrid** is a fast, distributed log analysis and search system (inspired by MapReduce/Splunk architecture). It is designed for parallel processing of gigabytes of text files distributed across multiple stateless compute nodes.

*This project is currently under active development (Work In Progress).*

## Architecture

The system is built on an event-driven, push-based asynchronous TCP/IP architecture and consists of two main components:

1. **Master (Server):** The central coordinator. It manages the task queue (file chunks), distributes workloads, and aggregates results. It features built-in **Fault Tolerance** – if an assigned Worker crashes or disconnects, its in-flight task is immediately reclaimed and redistributed to another active node.
2. **Worker (Client):** A stateless compute node. It connects to the Master, requests work, utilizes a local thread pool (`std::thread::hardware_concurrency`) for blazing-fast file parsing, and streams matching log lines back to the Master.

## Tech Stack

* **Language:** C++20 / C++23
* **Build System:** CMake + Ninja
* **Networking:** Asynchronous I/O based on Standalone Asio (`asio::io_context`).
* **Concurrency:** Modern C++ threading primitives (`std::mutex`, `std::atomic`, `std::thread`).

## Acknowledgments

The foundational networking layer (socket management, asynchronous thread-safe queues) is based on the `olc::net` architecture by [javidx9 (OneLoneCoder)](https://github.com/OneLoneCoder). `LogGrid` extends this core with a custom application protocol, robust fault tolerance, and multi-threaded data processing.
