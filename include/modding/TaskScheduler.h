#pragma once

#include "modding/ModSupport.h"

#include "mod/Scheduler.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
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

    /**
     * Runs task on a worker thread, at most MaxWorkers of them started on
     * demand, then posts onDone back for run to call on the main thread.
     */
    mod::Subscription async(size_t owner, mod::Scheduler::Task task, mod::Scheduler::Task onDone = {});
    void run(double now, const ErrorSink& errors);

    /**
     * Drops everything owner scheduled, waiting for its worker tasks that
     * already started, since their code lives in the mod's library.
     */
    void release(size_t owner);

    static constexpr size_t MaxWorkers = 2;

private:
    struct Entry {
        size_t owner = 0;
        double due = 0.0;
        double interval = 0.0;
        bool repeat = false;
        mod::Scheduler::Task task;
        std::shared_ptr<Handle> handle;
    };

    struct Job {
        size_t owner = 0;
        mod::Scheduler::Task task;
        mod::Scheduler::Task onDone;
        std::shared_ptr<Handle> handle;
    };

    void work();
    void runJob(Job& job);

    std::vector<std::shared_ptr<Entry>> tasks;
    std::mutex postedMutex;
    std::vector<std::pair<size_t, mod::Scheduler::Task>> posted;

    std::mutex jobMutex;
    std::condition_variable jobReady;
    std::condition_variable jobFinished;
    std::deque<Job> jobs;
    std::map<size_t, size_t> runningJobs;
    std::vector<std::thread> workers;
    size_t idleWorkers = 0;
    bool stopping = false;
};

}
