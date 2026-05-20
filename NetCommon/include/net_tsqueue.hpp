#ifndef NET_TSQUEUE_HPP
#define NET_TSQUEUE_HPP

#include "net_common.hpp"

#include <deque>
#include <mutex>

namespace olc
{
    namespace net
    {
        template <typename T>
        class tsqueue {
        public:
            tsqueue() = default;
            tsqueue(const tsqueue<T>& ob) = delete;
            virtual ~tsqueue() { clear(); }

            const T& front() {
                std::scoped_lock lock(muxQueue);
                return deqQueue.front();
            }

            const T& back() {
                std::scoped_lock lock(muxQueue);
                return deqQueue.back();
            }

            void push_back(const T& item) {
                std::scoped_lock lock(muxQueue);
                deqQueue.push_back(item);
            }

            void push_front(const T& item) {
                std::scoped_lock lock(muxQueue);
                deqQueue.push_front(item);
            }

            // Removes and returns item from the front of the Queue
            T pop_front() {
                std::scoped_lock lock(muxQueue);

                T item_to_remove = std::move(deqQueue.front());
                deqQueue.pop_front();
                return item_to_remove;
            }

            T pop_back() {
                std::scoped_lock lock(muxQueue);

                T item_to_remove = std::move(deqQueue.back());
                deqQueue.pop_back();
                return item_to_remove;
            }

            void clear() {
                std::scoped_lock lock(muxQueue);
                deqQueue.clear();
            }

            size_t count() const {
                std::scoped_lock lock(muxQueue);
                return deqQueue.size();
            }
            
            bool empty() const {
                std::scoped_lock lock(muxQueue);
                return deqQueue.empty();
            }
            
        protected:
            // Mutable allows to change mutex state even if it is defined in const method
            mutable std::mutex muxQueue;
            std::deque<T> deqQueue;
        };
    }
}

#endif // NET_TSQUEUE_HPP