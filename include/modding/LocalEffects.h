#pragma once

#include "mod/Effects.h"

#include <functional>
#include <map>

namespace kestrel::modding {

class LocalEffects {
public:
    using Request = mod::detail::EffectRequest;
    using Backend = std::function<void(Request&)>;

    explicit LocalEffects(Backend backend) : backend(std::move(backend)) { }
    void process(size_t owner, Request& request);
    void release(size_t owner);
    void clear();

private:
    struct Entry {
        size_t owner;
        Request::Kind kind;
        uint64_t native;
    };
    void remove(const Entry& entry);

    Backend backend;
    std::map<uint64_t, Entry> entries;
    uint64_t next = 0;
};

}
