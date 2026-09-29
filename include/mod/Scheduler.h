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
};

}
