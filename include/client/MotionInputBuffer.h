#pragma once

#include "client/PlayerMotion.h"

#include <array>
#include <cstddef>

namespace kestrel {

class MotionInputBuffer {
public:
    static constexpr size_t Capacity = 64;

    void push(const MotionInput& input)
    {
        if (input.jump != latest.jump || input.sneak != latest.sneak || input.sprint != latest.sprint) {
            if (count == Capacity) {
                head = (head + 1) % Capacity;
                --count;
            }
            transitions[(head + count) % Capacity] = { input.jump, input.sneak, input.sprint, input.trace };
            ++count;
        }
        if ((input.startGlide || input.stopGlide || input.startFlying || input.stopFlying) && requests.trace == 0) {
            requests.trace = input.trace;
        }
        requests.startGlide |= input.startGlide;
        requests.stopGlide |= input.stopGlide;
        requests.startFlying |= input.startFlying;
        requests.stopFlying |= input.stopFlying;
        latest = input;
    }

    MotionInput consume()
    {
        MotionInput input = latest;
        if (count != 0) {
            const auto& transition = transitions[head];
            input.jump = transition.jump;
            input.sneak = transition.sneak;
            input.sprint = transition.sprint;
            input.trace = transition.trace;
            head = (head + 1) % Capacity;
            --count;
        }
        input.startGlide = requests.startGlide;
        input.stopGlide = requests.stopGlide;
        input.startFlying = requests.startFlying;
        input.stopFlying = requests.stopFlying;
        if (requests.trace != 0 && (input.trace == 0 || requests.trace < input.trace)) input.trace = requests.trace;
        requests = {};
        return input;
    }

    MotionInput preview() const
    {
        MotionInput input = latest;
        if (count != 0) {
            const auto& transition = transitions[head];
            input.jump = transition.jump;
            input.sneak = transition.sneak;
            input.sprint = transition.sprint;
            input.trace = transition.trace;
        }
        input.startGlide = requests.startGlide;
        input.stopGlide = requests.stopGlide;
        input.startFlying = requests.startFlying;
        input.stopFlying = requests.stopFlying;
        if (requests.trace != 0 && (input.trace == 0 || requests.trace < input.trace)) input.trace = requests.trace;
        return input;
    }

    void reset(const MotionInput& input = {})
    {
        latest = input;
        head = 0;
        count = 0;
        requests = {};
    }

    size_t pending() const { return count; }

private:
    struct Transition {
        bool jump = false;
        bool sneak = false;
        bool sprint = false;
        uint64_t trace = 0;
    };

    std::array<Transition, Capacity> transitions {};
    MotionInput latest;
    MotionInput requests;
    size_t head = 0;
    size_t count = 0;
};

}
