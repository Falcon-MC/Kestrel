#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace kestrel::world {

class ChunkDecodeQueue {
public:
    using Apply = std::function<void()>;
    using Decode = std::function<Apply()>;

    ~ChunkDecodeQueue()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            ordered.clear();
            work.clear();
        }
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }

    void submit(size_t bytes, Decode decode)
    {
        std::lock_guard lock(mutex);
        auto task = reserve(bytes);
        task->decode = std::move(decode);
        work.push_back(task);
        if (!worker.joinable()) worker = std::thread([this] { run(); });
        wake.notify_one();
    }

    void append(size_t bytes, Apply apply)
    {
        std::lock_guard lock(mutex);
        auto task = reserve(bytes);
        task->apply = std::move(apply);
        task->ready = true;
    }

    void drain(std::chrono::steady_clock::duration budget = std::chrono::milliseconds(4))
    {
        // Applying a later block update first would let an older chunk overwrite it.
        const auto deadline = std::chrono::steady_clock::now() + budget;
        do {
            std::shared_ptr<Task> task;
            {
                std::lock_guard lock(mutex);
                if (ordered.empty() || !ordered.front()->ready) return;
                task = std::move(ordered.front());
                ordered.pop_front();
                queuedBytes -= task->bytes;
            }
            if (task->error) std::rethrow_exception(task->error);
            task->apply();
        } while (std::chrono::steady_clock::now() < deadline);
    }

    void clear()
    {
        std::lock_guard lock(mutex);
        ++generation;
        ordered.clear();
        work.clear();
        queuedBytes = 0;
    }

    bool empty() const
    {
        std::lock_guard lock(mutex);
        return ordered.empty();
    }

    bool backlogged() const
    {
        std::lock_guard lock(mutex);
        return ordered.size() >= MaxTasks / 2 || queuedBytes >= MaxBytes / 2;
    }

private:
    static constexpr size_t MaxTasks = 1024;
    static constexpr size_t MaxBytes = 64 * 1024 * 1024;
    struct Task {
        Decode decode;
        Apply apply;
        std::exception_ptr error;
        size_t bytes = 0;
        uint64_t generation = 0;
        bool ready = false;
    };

    std::shared_ptr<Task> reserve(size_t bytes)
    {
        if (ordered.size() >= MaxTasks || bytes > MaxBytes - queuedBytes)
            throw std::runtime_error("Chunk decode queue capacity exceeded");
        auto task = std::make_shared<Task>();
        task->bytes = bytes;
        task->generation = generation;
        ordered.push_back(task);
        queuedBytes += bytes;
        return task;
    }

    void run()
    {
        for (;;) {
            std::shared_ptr<Task> task;
            Decode decode;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !work.empty(); });
                if (stopping) return;
                task = std::move(work.front());
                work.pop_front();
                decode = std::move(task->decode);
            }
            Apply apply;
            std::exception_ptr error;
            try { apply = decode(); }
            catch (...) { error = std::current_exception(); }
            {
                std::lock_guard lock(mutex);
                if (task->generation != generation || stopping) continue;
                task->apply = std::move(apply);
                task->error = error;
                task->ready = true;
            }
        }
    }

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Task>> ordered, work;
    size_t queuedBytes = 0;
    uint64_t generation = 0;
    bool stopping = false;
    std::thread worker;
};

}
