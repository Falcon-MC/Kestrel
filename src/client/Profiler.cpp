#include "client/Profiler.h"

#include "client/DebugLog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace kestrel {

namespace {

double millisecondsBetween(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

}

Profiler::Section::Section(Profiler& owner, const char* name)
    : profiler(owner)
    , index(owner.sectionIndex(name))
    , start(std::chrono::steady_clock::now())
{
}

Profiler::Section::~Section()
{
    profiler.add(index, millisecondsBetween(start, std::chrono::steady_clock::now()));
}

size_t Profiler::sectionIndex(const char* name)
{
    for (size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].name == name || std::strcmp(entries[index].name, name) == 0) {
            return index;
        }
    }
    Entry entry;
    entry.name = name;
    entries.push_back(entry);
    return entries.size() - 1;
}

void Profiler::add(size_t index, double milliseconds)
{
    entries[index].frameTotal += milliseconds;
}

void Profiler::beginFrame()
{
    frameStart = std::chrono::steady_clock::now();
    if (!started) {
        started = true;
        windowStart = frameStart;
    }
}

void Profiler::endFrame()
{
    auto now = std::chrono::steady_clock::now();
    double frame = millisecondsBetween(frameStart, now);
    frameSum += frame;
    frameWorst = std::max(frameWorst, frame);
    ++frames;
    for (Entry& entry : entries) {
        entry.sum += entry.frameTotal;
        entry.worst = std::max(entry.worst, entry.frameTotal);
        entry.frameTotal = 0.0;
    }
    if (millisecondsBetween(windowStart, now) >= 1000.0) {
        double seconds = millisecondsBetween(windowStart, now) / 1000.0;
        publish();
        published.front() = [&] {
            char text[128];
            std::snprintf(text, sizeof(text), "Profiler  %.0f fps  \xC2\xB7  frame %.2f ms avg  %.2f ms worst", frames / seconds, frameSum / double(frames), frameWorst);
            return std::string(text);
        }();
        windowStart = now;
        frameSum = 0.0;
        frameWorst = 0.0;
        frames = 0;
        for (Entry& entry : entries) {
            entry.sum = 0.0;
            entry.worst = 0.0;
        }
    }
}

void Profiler::publish()
{
    std::vector<const Entry*> order;
    for (const Entry& entry : entries) {
        order.push_back(&entry);
    }
    std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
        return a->sum > b->sum;
    });
    published.assign(1, std::string());
    double count = frames > 0 ? double(frames) : 1.0;
    double frameAverage = frameSum / count;
    for (const Entry* entry : order) {
        double average = entry->sum / count;
        double share = frameAverage > 0.0 ? average / frameAverage * 100.0 : 0.0;
        char text[128];
        std::snprintf(text, sizeof(text), "%-14s %6.2f ms  %5.1f%%  worst %6.2f", entry->name, average, share, entry->worst);
        published.emplace_back(text);
    }
}

}
