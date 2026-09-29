#include "TaskScheduler.h"

#include <algorithm>

namespace kestrel::modding {

TaskScheduler::~TaskScheduler()
{
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
    std::lock_guard<std::mutex> guard(postedMutex);
    std::erase_if(posted, [owner](const auto& task) { return task.first == owner; });
}

}
