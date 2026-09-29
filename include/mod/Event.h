#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>

namespace kestrel::mod {

/**
 * Base of everything that goes through the event bus. Events are told apart
 * by their Type string rather than RTTI, which does not survive the trip
 * between the client and a mod library on every platform.
 */
class Event {
public:
    virtual ~Event() = default;
    virtual std::string_view type() const = 0;

    virtual bool isCancelled() const
    {
        return false;
    }
};

class CancellableEvent : public Event {
public:
    void cancel(bool value = true)
    {
        cancelled = value;
    }

    bool isCancelled() const override
    {
        return cancelled;
    }

private:
    bool cancelled = false;
};

// Put this inside an event struct, with a name like "mymod:thing_happened".
#define KESTREL_EVENT(name)                          \
    static constexpr std::string_view Type = name;   \
    std::string_view type() const override           \
    {                                                \
        return Type;                                 \
    }

/**
 * Handlers run from Lowest to Highest. Monitor comes last and is meant for
 * looking at the outcome, not changing it.
 */
enum class Priority {
    Lowest,
    Low,
    Normal,
    High,
    Highest,
    Monitor,
};

struct ListenOptions {
    Priority priority = Priority::Normal;
    // Cancelled events skip the handler unless this is set.
    bool receiveCancelled = false;
};

/**
 * Something a mod registered: a listener, a command, a key binding, a task
 * or a packet filter. Everything a mod registers goes away by itself when the
 * mod is unloaded, so keeping the handle is only needed to stop it earlier.
 */
class Subscription {
public:
    class State {
    public:
        virtual ~State() = default;
        virtual void cancel() = 0;
        virtual bool active() const = 0;
    };

    Subscription() = default;

    explicit Subscription(std::shared_ptr<State> state)
        : state(std::move(state))
    {
    }

    void cancel()
    {
        if (state) {
            state->cancel();
        }
    }

    bool active() const
    {
        return state && state->active();
    }

private:
    std::shared_ptr<State> state;
};

class EventBus {
public:
    using Handler = std::function<void(Event&)>;

    virtual ~EventBus() = default;

    virtual Subscription subscribe(std::string_view type, Handler handler, ListenOptions options = {}) = 0;

    /**
     * Runs every mod's handlers for the event right away, on the calling
     * thread. Mods can post their own event types to talk to each other.
     */
    virtual void post(Event& event) = 0;

    template <class E, class F>
    Subscription on(F&& handler, ListenOptions options = {})
    {
        static_assert(std::is_base_of_v<Event, E>, "events derive from kestrel::mod::Event");
        return subscribe(E::Type, [handler = std::forward<F>(handler)](Event& event) mutable { handler(static_cast<E&>(event)); }, options);
    }
};

}
