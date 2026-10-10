#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace kestrel {

class InputLatency {
public:
    enum Stage { Camera, Physics, Network, Submission, FrameEnd, StageCount };
    struct Sample {
        uint64_t id = 0;
        double received = 0;
        double nativeReceipt = 0;
        std::array<double, StageCount> stages {};
    };
    struct Distribution {
        size_t count = 0;
        double median = 0;
        double p95 = 0;
        double p99 = 0;
        double maximum = 0;
    };
    struct Snapshot {
        std::array<Distribution, StageCount> stages;
        std::array<Distribution, StageCount> nativeStages;
        Distribution tickDelay;
        Distribution gpuWait;
        Distribution beginFrame;
        Distribution presentation;
        uint64_t corrections = 0;
    };

    static double now()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    uint64_t begin(double received, double nativeReceipt = 0)
    {
        std::lock_guard lock(mutex);
        uint64_t id = ++sequence;
        samples[id % Capacity] = { id, received, nativeReceipt, {} };
        return id;
    }

    void mark(uint64_t id, Stage stage)
    {
        markAt(id, stage, now());
    }

    void markAt(uint64_t id, Stage stage, double stamp)
    {
        if (!id || stamp <= 0.0) return;
        std::lock_guard lock(mutex);
        Sample& sample = samples[id % Capacity];
        if (sample.id == id && !sample.stages[stage]) sample.stages[stage] = stamp;
    }

    void tick(double delay)
    {
        std::lock_guard lock(mutex);
        tickDelays[ticks++ % Capacity] = std::max(0.0, delay) * 1000.0;
    }

    void waitedForGpu(double milliseconds, double beginMilliseconds = 0.0, double presentMilliseconds = 0.0)
    {
        std::lock_guard lock(mutex);
        size_t index = frames++ % Capacity;
        gpuWaits[index] = milliseconds;
        beginCosts[index] = beginMilliseconds;
        presentationCosts[index] = presentMilliseconds;
    }

    void corrected()
    {
        std::lock_guard lock(mutex);
        ++corrections;
    }

    void reset()
    {
        std::lock_guard lock(mutex);
        samples = {};
        ticks = frames = corrections = 0;
    }

    Snapshot snapshot() const
    {
        std::array<std::vector<double>, StageCount> values;
        std::array<std::vector<double>, StageCount> nativeValues;
        std::vector<double> delays, waits, beginnings, presents;
        Snapshot result;
        {
            std::lock_guard lock(mutex);
            for (const auto& sample : samples) {
                if (!sample.id) continue;
                for (size_t stage = 0; stage < StageCount; ++stage) {
                    if (sample.stages[stage] >= sample.received && sample.stages[stage] > 0) {
                        values[stage].push_back((sample.stages[stage] - sample.received) * 1000.0);
                    }
                    if (sample.nativeReceipt > 0.0 && sample.stages[stage] >= sample.nativeReceipt) {
                        nativeValues[stage].push_back((sample.stages[stage] - sample.nativeReceipt) * 1000.0);
                    }
                }
            }
            delays.assign(tickDelays.begin(), tickDelays.begin() + std::min(ticks, Capacity));
            waits.assign(gpuWaits.begin(), gpuWaits.begin() + std::min(frames, Capacity));
            beginnings.assign(beginCosts.begin(), beginCosts.begin() + std::min(frames, Capacity));
            presents.assign(presentationCosts.begin(), presentationCosts.begin() + std::min(frames, Capacity));
            result.corrections = corrections;
        }
        for (size_t stage = 0; stage < StageCount; ++stage) {
            result.stages[stage] = distribution(values[stage]);
            result.nativeStages[stage] = distribution(nativeValues[stage]);
        }
        result.tickDelay = distribution(delays);
        result.gpuWait = distribution(waits);
        result.beginFrame = distribution(beginnings);
        result.presentation = distribution(presents);
        return result;
    }

private:
    static Distribution distribution(std::vector<double>& values)
    {
        if (values.empty()) return {};
        std::sort(values.begin(), values.end());
        auto at = [&](double fraction) { return values[size_t((values.size() - 1) * fraction)]; };
        return { values.size(), at(0.5), at(0.95), at(0.99), values.back() };
    }

    static constexpr size_t Capacity = 4096;
    mutable std::mutex mutex;
    std::array<Sample, Capacity> samples {};
    std::array<double, Capacity> tickDelays {}, gpuWaits {};
    std::array<double, Capacity> beginCosts {}, presentationCosts {};
    uint64_t sequence = 0;
    size_t ticks = 0, frames = 0;
    uint64_t corrections = 0;
};

}
