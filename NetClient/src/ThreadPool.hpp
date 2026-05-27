#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; i++) {
            m_workerThreads.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(m_queueState);
                        
                        // To avoid Spurious Wakeups wait() will check if queue is not empty
                        m_conditionVar.wait(lock, [this]() { return m_stop || !m_taskQueue.empty(); });

                        // If m_stop flag was set, kill process (Graceful)
                        if (m_stop && m_taskQueue.empty())
                            return;

                        task = m_taskQueue.front();
                        m_taskQueue.pop();
                    }

                    task();
                }
            });
        }
    }
    
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(m_queueState);
            m_stop = true;
        }

        m_conditionVar.notify_all();
    }

public:
    void Enqueue(std::function<void()> taskProcessing) {
        {
            std::lock_guard<std::mutex> lock(m_queueState);
            m_taskQueue.push(taskProcessing);
        }
        m_conditionVar.notify_one();
    }
    
private:
    std::vector<std::jthread> m_workerThreads;
    std::queue<std::function<void()>> m_taskQueue;
    std::mutex m_queueState;
    std::condition_variable m_conditionVar;
    bool m_stop = false;
};

#endif // THREAD_POOL_HPP