#pragma once

#include "mod/Event.h"

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <string_view>

namespace kestrel::modding {

/**
 * Owner 0 is the client itself; mods count from 1.
 */
inline constexpr size_t HostOwner = 0;

using ErrorSink = std::function<void(size_t owner, std::string_view what)>;

/**
 * The state behind a mod's Subscription. The registry that made it passes
 * release, which takes the registration away when the mod cancels; expire is
 * for the registry dropping it by itself.
 */
class Handle final : public mod::Subscription::State {
public:
    explicit Handle(std::function<void()> release)
        : release(std::move(release))
    {
    }

    void cancel() override
    {
        std::function<void()> action;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (!alive) {
                return;
            }
            alive = false;
            action = std::move(release);
        }
        if (action) {
            action();
        }
    }

    bool active() const override
    {
        return alive;
    }

    void expire()
    {
        std::lock_guard<std::mutex> guard(mutex);
        alive = false;
        release = nullptr;
    }

private:
    std::mutex mutex;
    std::atomic<bool> alive { true };
    std::function<void()> release;
};

/**
 * Runs mod code and turns whatever it throws into a report, so one broken
 * mod cannot take the client down with it.
 */
template <class F>
void guarded(const ErrorSink& errors, size_t owner, F&& call)
{
    try {
        call();
    } catch (const std::exception& failure) {
        errors(owner, failure.what());
    } catch (...) {
        errors(owner, "unknown exception");
    }
}

}
