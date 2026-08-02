# Contributing to LogGrid

First off — thank you for taking the time to contribute! Every bit of help, whether it's a bug report, a documentation fix, or a new test, makes LogGrid better for everyone.

If you're new to the project or to open source in general, look for issues tagged [`good first issue`](../../issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) or [`help wanted`](../../issues?q=is%3Aissue+is%3Aopen+label%3A%22help+wanted%22) — those are the best places to start.

---

## 🎓 Academic Notice

LogGrid is currently an active **engineering thesis project**. To preserve academic integrity, the **core architectural components must be designed and implemented by the author independently**. This includes:

- The Master-Worker orchestration logic
- Fault Tolerance algorithms (task reclaim and redistribution)
- The distributed task scheduling protocol
- The result aggregation pipeline

**Pull Requests that touch these areas will not be merged**, but they are absolutely welcome as discussions in a GitHub Issue. If you have an idea for a core improvement, open an issue and let's talk about it — your input may shape the direction of the project.

All other areas (see below) are open for community contributions without restriction.

---

## Where Your Help Is Most Welcome

These areas are **fully open** for community contributions:

| Area | Description |
|---|---|
| **Unit & Integration Tests** | Adding GoogleTest coverage for `MasterServer`, `WorkerClient`, `ThreadPool`, `FileProcessor` |
| **Docker & CI/CD** | Improving `Dockerfile` multi-stage builds, adding GitHub Actions pipelines, linting, or build matrix |
| **Documentation** | Doxygen docstrings, improving README, adding architecture diagrams |
| **Bug Fixes** | Reproducing and fixing reported bugs — check the [`bug`](../../issues?q=is%3Aissue+label%3Abug) label |
| **CLI / Output** | Improving console output formatting, adding verbosity flags, structured logging |

> **Planning a large architectural change?** Please open an Issue first to discuss it before investing time in a PR. This avoids duplicate effort and keeps the project direction consistent.

---

## 🛠️ Development Environment

The recommended way to run LogGrid is via Docker Compose, which starts the full Master + Worker cluster in one command.

**Prerequisites:** Docker, Docker Compose

```bash
# Clone the repository
git clone https://github.com/MikolajKos/LogGrid.git
cd LogGrid

# Build and start the full cluster (Master + 2 Workers + log generator)
docker compose up --build

# Follow live logs from all containers
docker compose logs -f

# Tear down
docker compose down
```

**Building a single image manually:**
```bash
# Master node
docker build -f Dockerfile.master -t loggrid-master .

# Worker node
docker build -f Dockerfile.worker -t loggrid-worker .
```

**Building locally without Docker (requires CMake ≥ 3.25 and a C++20 compiler):**
```bash
cd LogGrid
mkdir build
cd build

cmake .. -G "Ninja"
cmake --build .
```

---

## Code Standards

To keep the codebase consistent and maintainable, please follow these guidelines:

- **Modern C++:** Prefer C++17/20 features. Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) over raw pointers. Avoid manual memory management.
- **Thread Safety:** Any shared state must be protected by a mutex. Clearly document locking contracts in comments when a function must be called with or without a lock held.
- **Doxygen Comments:** Document all public methods and non-trivial private methods using Doxygen-style docstrings:
  ```cpp
  /**
   * @brief Short description of what the function does.
   * @param client Shared pointer to the requesting client connection.
   * @return true if a task was dispatched, false otherwise.
   */
  bool DispatchNextTask(std::shared_ptr<olc::net::connection<LogSystem::LogSearchMsg>> client);
  ```
- **No warnings:** The project compiles with `-Wall -Wextra -Wpedantic -Werror`. Your code must compile cleanly.
- **Formatting:** Match the existing style (4-space indentation, braces on the same line for control flow).

---

## Pull Request Workflow

1. **Fork** the repository and create your branch from `main`:
   ```bash
   git checkout -b fix/descriptive-branch-name
   ```

2. **Make your changes** and ensure the project still builds cleanly.

3. **Write descriptive commits** using [Conventional Commits](https://www.conventionalcommits.org/) format:
   ```
   fix(fault-tolerance): correct task reclaim when worker disconnects mid-search
   docs(readme): add architecture diagram link
   test(threadpool): add unit tests for graceful shutdown
   ```

4. **Open a Pull Request** against the `main` branch. Fill in the PR template with:
   - What problem does this PR solve?
   - How was it tested?
   - Any relevant Issue numbers (`Closes #42`)

5. **Respond to review feedback** — all PRs are reviewed before merging.

---

## 📬 Questions?

Open a [GitHub Discussion](../../discussions) or file an [Issue](../../issues) — happy to help you get started.
