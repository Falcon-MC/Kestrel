#include "modding/TaskScheduler.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace kestrel::modding {

TaskScheduler::~TaskScheduler()
{
    {
        std::lock_guard<std::mutex> guard(jobMutex);
        stopping = true;
        for (Job& job : jobs) {
            job.handle->expire();
        }
        jobs.clear();
    }
    jobReady.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (const std::shared_ptr<Entry>& entry : tasks) {
        entry->handle->expire();
    }
}

mod::Subscription TaskScheduler::schedule(size_t owner, double now, double delay, bool repeat, mod::Scheduler::Task task)
{
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->interval = std::max(delay, 0.0);
    entry->due = now + entry->interval;
    entry->repeat = repeat;
    entry->task = std::move(task);
    // Cancelled tasks are swept by run, which keeps this safe to call from a task.
    entry->handle = std::make_shared<Handle>(nullptr);
    tasks.push_back(entry);
    return mod::Subscription(entry->handle);
}

void TaskScheduler::post(size_t owner, mod::Scheduler::Task task)
{
    std::lock_guard<std::mutex> guard(postedMutex);
    posted.emplace_back(owner, std::move(task));
}

mod::Subscription TaskScheduler::async(size_t owner, mod::Scheduler::Task task, mod::Scheduler::Task onDone)
{
    auto handle = std::make_shared<Handle>(nullptr);
    if (!task) {
        handle->expire();
        return mod::Subscription(handle);
    }
    {
        std::lock_guard<std::mutex> guard(jobMutex);
        if (idleWorkers == 0 && workers.size() < MaxWorkers) {
            workers.emplace_back([this] {
                work();
            });
        }
        jobs.push_back({ owner, std::move(task), std::move(onDone), handle });
    }
    jobReady.notify_one();
    return mod::Subscription(handle);
}

void TaskScheduler::work()
{
    std::unique_lock<std::mutex> lock(jobMutex);
    while (true) {
        ++idleWorkers;
        jobReady.wait(lock, [this] {
            return stopping || !jobs.empty();
        });
        --idleWorkers;
        if (stopping) {
            return;
        }
        Job job = std::move(jobs.front());
        jobs.pop_front();
        size_t owner = job.owner;
        ++runningJobs[owner];
        lock.unlock();
        runJob(job);
        job = {};
        lock.lock();
        if (--runningJobs[owner] == 0) {
            runningJobs.erase(owner);
        }
        jobFinished.notify_all();
    }
}

/**
 * Runs one job on its worker. A failure travels to the main thread as a
 * posted task that throws, so it is reported there like any other.
 */
void TaskScheduler::runJob(Job& job)
{
    if (!job.handle->active()) {
        return;
    }
    std::string failure;
    try {
        job.task();
    } catch (const std::exception& error) {
        failure = error.what();
    } catch (...) {
        failure = "unknown exception";
    }
    if (!failure.empty()) {
        job.handle->expire();
        post(job.owner, [message = "async task threw: " + failure] {
            throw std::runtime_error(message);
        });
        return;
    }
    if (!job.onDone) {
        job.handle->expire();
        return;
    }
    post(job.owner, [done = std::move(job.onDone), handle = job.handle] {
        if (!handle->active()) {
            return;
        }
        handle->expire();
        done();
    });
}

void TaskScheduler::run(double now, const ErrorSink& errors)
{
    std::vector<std::pair<size_t, mod::Scheduler::Task>> ready;
    {
        std::lock_guard<std::mutex> guard(postedMutex);
        ready.swap(posted);
    }
    for (auto& [owner, task] : ready) {
        guarded(errors, owner, task);
    }

    std::vector<std::shared_ptr<Entry>> due;
    for (const std::shared_ptr<Entry>& entry : tasks) {
        if (entry->handle->active() && entry->due <= now) {
            due.push_back(entry);
        }
    }
    for (const std::shared_ptr<Entry>& entry : due) {
        if (!entry->handle->active()) {
            continue;
        }
        guarded(errors, entry->owner, entry->task);
        if (entry->repeat) {
            // After a hitch a repeating task runs once, not once per missed period.
            entry->due = entry->interval > 0.0 ? std::max(entry->due + entry->interval, now) : now + 1e-9;
        } else {
            entry->handle->expire();
        }
    }
    std::erase_if(tasks, [](const std::shared_ptr<Entry>& entry) { return !entry->handle->active(); });
}

void TaskScheduler::release(size_t owner)
{
    for (const std::shared_ptr<Entry>& entry : tasks) {
        if (entry->owner == owner) {
            entry->handle->expire();
        }
    }
    std::erase_if(tasks, [owner](const std::shared_ptr<Entry>& entry) { return entry->owner == owner; });
    {
        std::unique_lock<std::mutex> lock(jobMutex);
        std::erase_if(jobs, [owner](const Job& job) {
            if (job.owner != owner) {
                return false;
            }
            job.handle->expire();
            return true;
        });
        jobFinished.wait(lock, [this, owner] {
            return !runningJobs.contains(owner);
        });
    }
    std::lock_guard<std::mutex> guard(postedMutex);
    std::erase_if(posted, [owner](const auto& task) { return task.first == owner; });
}

}
