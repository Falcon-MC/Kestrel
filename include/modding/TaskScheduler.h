#pragma once

#include "modding/ModSupport.h"

#include "mod/Scheduler.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace kestrel::modding {

class TaskScheduler {
public:
    TaskScheduler() = default;
    ~TaskScheduler();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    /**
     * Runs task delay seconds from now, then again every delay seconds when
     * repeat is set; a repeating task with no delay runs every frame.
     */
    mod::Subscription schedule(size_t owner, double now, double delay, bool repeat, mod::Scheduler::Task task);
    void post(size_t owner, mod::Scheduler::Task task);
    void run(double now, const ErrorSink& errors);
    void release(size_t owner);

private:
    struct Entry {
        size_t owner = 0;
        double due = 0.0;
        double interval = 0.0;
        bool repeat = false;
        mod::Scheduler::Task task;
        std::shared_ptr<Handle> handle;
    };

    std::vector<std::shared_ptr<Entry>> tasks;
    std::mutex postedMutex;
    std::vector<std::pair<size_t, mod::Scheduler::Task>> posted;
};

}
