#pragma once

#include "modding/ModSupport.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kestrel::modding {

class EventDispatcher {
public:
    explicit EventDispatcher(ErrorSink errors);
    ~EventDispatcher();

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    mod::Subscription subscribe(size_t owner, std::string_view type, mod::EventBus::Handler handler, mod::ListenOptions options);
    void dispatch(mod::Event& event);
    void dispatchTo(size_t owner, mod::Event& event);
    bool listening(std::string_view type) const;
    void release(size_t owner);

private:
    struct Listener {
        size_t owner = 0;
        mod::ListenOptions options;
        mod::EventBus::Handler handler;
        std::shared_ptr<Handle> handle;
    };

    using Listeners = std::vector<std::shared_ptr<Listener>>;

    void remove(const std::string& type, const Listener* listener);

    ErrorSink errors;
    std::map<std::string, Listeners, std::less<>> listeners;
};

}
