#include "modding/EventDispatcher.h"

#include <algorithm>

namespace kestrel::modding {

EventDispatcher::EventDispatcher(ErrorSink errors)
    : errors(std::move(errors))
{
}

EventDispatcher::~EventDispatcher()
{
    for (auto& [type, list] : listeners) {
        for (const std::shared_ptr<Listener>& listener : list) {
            listener->handle->expire();
        }
    }
}

mod::Subscription EventDispatcher::subscribe(size_t owner, std::string_view type, mod::EventBus::Handler handler, mod::ListenOptions options)
{
    auto listener = std::make_shared<Listener>();
    listener->owner = owner;
    listener->options = options;
    listener->handler = std::move(handler);
    std::string key(type);
    listener->handle = std::make_shared<Handle>([this, key, raw = listener.get()] { remove(key, raw); });

    Listeners& list = listeners[key];
    // Same priority keeps the order of registration.
    auto place = std::upper_bound(list.begin(), list.end(), options.priority, [](mod::Priority priority, const std::shared_ptr<Listener>& other) {
        return priority < other->options.priority;
    });
    list.insert(place, listener);
    return mod::Subscription(listener->handle);
}

void EventDispatcher::dispatch(mod::Event& event)
{
    auto found = listeners.find(event.type());
    if (found == listeners.end()) {
        return;
    }
    // A handler may subscribe or cancel while this runs, so walk a copy.
    Listeners snapshot = found->second;
    for (const std::shared_ptr<Listener>& listener : snapshot) {
        if (!listener->handle->active() || (event.isCancelled() && !listener->options.receiveCancelled)) {
            continue;
        }
        guarded(errors, listener->owner, [&] { listener->handler(event); });
    }
}

void EventDispatcher::dispatchTo(size_t owner, mod::Event& event)
{
    auto found = listeners.find(event.type());
    if (found == listeners.end()) return;
    Listeners snapshot = found->second;
    for (const auto& listener : snapshot) {
        if (listener->owner != owner || !listener->handle->active() || (event.isCancelled() && !listener->options.receiveCancelled)) continue;
        guarded(errors, owner, [&] { listener->handler(event); });
    }
}

bool EventDispatcher::listening(std::string_view type) const
{
    auto found = listeners.find(type);
    return found != listeners.end() && !found->second.empty();
}

void EventDispatcher::release(size_t owner)
{
    for (auto& [type, list] : listeners) {
        std::erase_if(list, [owner](const std::shared_ptr<Listener>& listener) {
            if (listener->owner != owner) {
                return false;
            }
            listener->handle->expire();
            return true;
        });
    }
}

void EventDispatcher::remove(const std::string& type, const Listener* listener)
{
    auto found = listeners.find(type);
    if (found != listeners.end()) {
        std::erase_if(found->second, [listener](const std::shared_ptr<Listener>& entry) { return entry.get() == listener; });
    }
}

}
