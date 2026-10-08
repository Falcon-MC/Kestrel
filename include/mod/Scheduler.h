#pragma once

#include "mod/Event.h"

#include <functional>

namespace kestrel::mod {

/**
 * Runs code later on the main thread.
 */
class Scheduler {
public:
    using Task = std::function<void()>;

    virtual ~Scheduler() = default;

    virtual Subscription after(double seconds, Task task) = 0;
    virtual Subscription every(double seconds, Task task) = 0;

    /**
     * Hands a task to the main thread from any thread, the way a packet
     * filter or a mod's own thread gets back to the rest of the API.
     */
    virtual void post(Task task) = 0;

    Subscription nextFrame(Task task)
    {
        return after(0.0, std::move(task));
    }

    // Added in API 4 and kept last so older mods still find everything above.
    /**
     * Runs task on one of the client's two worker threads, then onDone on
     * the main thread. The task must not use the rest of the API; it gets
     * back to the main thread through post or onDone. Cancelling drops a
     * task that has not started and the onDone of one that has. onDone does
     * not run when the task throws, the error is reported like any other.
     * Unloading the mod waits for its running tasks, so a task must never
     * wait for the main thread.
     */
    virtual Subscription async(Task task, Task onDone = {}) = 0;
};

}
