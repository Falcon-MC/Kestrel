#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace kestrel {

/**
 * Times the named sections of each frame on the main thread and publishes,
 * once per second, the average and worst milliseconds of every section, the
 * whole frame and the frame rate, heaviest section first.
 */
class Profiler {
public:
    class Section {
    public:
        Section(Profiler& owner, const char* name);
        ~Section();

        Section(const Section&) = delete;
        Section& operator=(const Section&) = delete;

    private:
        Profiler& profiler;
        size_t index;
        std::chrono::steady_clock::time_point start;
    };

    void beginFrame();
    void endFrame();

    const std::vector<std::string>& lines() const
    {
        return published;
    }

private:
    struct Entry {
        const char* name = "";
        double frameTotal = 0.0;
        double sum = 0.0;
        double worst = 0.0;
    };

    size_t sectionIndex(const char* name);
    void add(size_t index, double milliseconds);
    void publish();

    std::vector<Entry> entries;
    std::chrono::steady_clock::time_point frameStart;
    std::chrono::steady_clock::time_point windowStart;
    double frameSum = 0.0;
    double frameWorst = 0.0;
    uint64_t frames = 0;
    bool started = false;
    bool firstLogged = false;
    std::vector<std::string> published;
};

}
