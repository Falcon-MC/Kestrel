#include "modding/TaskScheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace kestrel;
using namespace kestrel::modding;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

/**
 * Runs the scheduler like the client's frames do until done holds, failing
 * after a few seconds.
 */
template <class Done>
void runUntil(TaskScheduler& scheduler, const ErrorSink& errors, Done done, const char* message)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done()) {
        check(std::chrono::steady_clock::now() < deadline, message);
        scheduler.run(0.0, errors);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main()
{
    std::string reported;
    size_t reportedOwner = 0;
    ErrorSink errors = [&](size_t owner, std::string_view what) {
        reportedOwner = owner;
        reported = what;
    };
    std::thread::id mainThread = std::this_thread::get_id();

    {
        TaskScheduler scheduler;
        std::atomic<bool> ran { false };
        std::thread::id worker;
        std::thread::id finisher;
        bool finished = false;
        mod::Subscription job = scheduler.async(1, [&] {
            worker = std::this_thread::get_id();
            ran = true;
        }, [&] {
            finisher = std::this_thread::get_id();
            finished = true;
        });
        runUntil(scheduler, errors, [&] { return finished; }, "onDone arrives through run");
        check(ran && worker != mainThread, "the task runs on a worker thread");
        check(finisher == mainThread, "onDone runs on the main thread");
        check(!job.active(), "a finished job is no longer active");
    }

    {
        TaskScheduler scheduler;
        std::atomic<bool> release { false };
        std::atomic<int> started { 0 };
        std::atomic<int> running { 0 };
        std::atomic<int> most { 0 };
        std::atomic<int> done { 0 };
        for (int i = 0; i < 6; ++i) {
            scheduler.async(1, [&] {
                ++started;
                int now = ++running;
                int seen = most;
                while (now > seen && !most.compare_exchange_weak(seen, now)) {
                }
                while (!release) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                --running;
                ++done;
            });
        }
        runUntil(scheduler, errors, [&] { return started >= 2; }, "two workers start");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(started == 2, "no more than two tasks run at once");
        release = true;
        runUntil(scheduler, errors, [&] { return done == 6; }, "queued tasks all run");
        check(most <= static_cast<int>(TaskScheduler::MaxWorkers), "the pool stays at two workers");
    }

    {
        TaskScheduler scheduler;
        std::atomic<bool> release { false };
        std::atomic<bool> blocking { false };
        bool cancelledRan = false;
        bool cancelledDone = false;
        scheduler.async(1, [&] {
            blocking = true;
            while (!release) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        scheduler.async(1, [&] {
            while (!release) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        mod::Subscription queued = scheduler.async(1, [&] {
            cancelledRan = true;
        }, [&] {
            cancelledDone = true;
        });
        runUntil(scheduler, errors, [&] { return blocking.load(); }, "the blocking task starts");
        queued.cancel();
        release = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        scheduler.run(0.0, errors);
        check(!cancelledRan && !cancelledDone, "a cancelled task that had not started never runs");
    }

    {
        TaskScheduler scheduler;
        std::atomic<bool> started { false };
        std::atomic<bool> finished { false };
        bool doneRan = false;
        scheduler.async(2, [&] {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            finished = true;
        }, [&] {
            doneRan = true;
        });
        while (!started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        scheduler.release(2);
        check(finished, "release waits for the owner's running task");
        scheduler.run(0.0, errors);
        check(!doneRan, "release drops the owner's pending onDone");
    }

    {
        TaskScheduler scheduler;
        bool doneRan = false;
        scheduler.async(3, [] {
            throw std::runtime_error("broken");
        }, [&] {
            doneRan = true;
        });
        runUntil(scheduler, errors, [&] { return !reported.empty(); }, "a throwing task is reported");
        check(reportedOwner == 3 && reported.find("broken") != std::string::npos, "the report names the owner and the error");
        check(!doneRan, "onDone does not run after the task threw");
    }

    {
        TaskScheduler scheduler;
        scheduler.async(4, [] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        });
    }
}
