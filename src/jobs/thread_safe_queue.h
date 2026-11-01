#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

template<typename T, typename Compare>
class ThreadSafeQueue {
private:
    std::priority_queue<T, std::vector<T>, Compare> queue;
    mutable std::mutex mutex;
    std::condition_variable condVar;
    bool shutdown = false;

public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shutdown) return;
            queue.push(std::move(item));
        }
        condVar.notify_one();
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex);
        condVar.wait(lock, [this] { return !queue.empty() || shutdown; });
        if (shutdown && queue.empty()) return false;
        item = std::move(const_cast<T&>(queue.top()));  // safe: popped immediately after
        queue.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown = true;
        }
        condVar.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
};