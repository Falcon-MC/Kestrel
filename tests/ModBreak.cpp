#include "client/BlockBreaker.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

using namespace kestrel;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

/**
 * Everything a run of breaking ticks sent and reported, kept in order.
 */
struct Trace {
    std::vector<BreakAction> actions;
    std::vector<BreakProgress> progress;
};

bool sameActions(const Trace& a, const Trace& b)
{
    if (a.actions.size() != b.actions.size() || a.progress.size() != b.progress.size()) {
        return false;
    }
    for (size_t i = 0; i < a.actions.size(); ++i) {
        if (a.actions[i].kind != b.actions[i].kind || a.actions[i].cell != b.actions[i].cell || a.actions[i].face != b.actions[i].face) {
            return false;
        }
    }
    for (size_t i = 0; i < a.progress.size(); ++i) {
        const BreakProgress& left = a.progress[i];
        const BreakProgress& right = b.progress[i];
        if (left.cell != right.cell || left.progress != right.progress || left.finished != right.finished || left.aborted != right.aborted) {
            return false;
        }
    }
    return true;
}

/**
 * One breaking tick per entry: whether attack is held and the block hit.
 */
Trace run(const std::vector<std::pair<bool, std::optional<BreakHit>>>& ticks, float speed)
{
    BlockBreaker breaker;
    Trace trace;
    for (const auto& [held, hit] : ticks) {
        BreakStep step = breaker.step(held, held ? hit : std::nullopt, speed, false, false);
        trace.actions.insert(trace.actions.end(), step.actions.begin(), step.actions.end());
        trace.progress.insert(trace.progress.end(), step.progress.begin(), step.progress.end());
    }
    return trace;
}

constexpr std::array<int32_t, 3> Cell { 1, 64, 2 };
constexpr std::array<int32_t, 3> Other { 2, 64, 2 };
constexpr int32_t Up = 1;
constexpr uint32_t Stone = 7;

BreakHit crosshairHit(const std::array<int32_t, 3>& cell)
{
    return { cell, Up, Stone, { cell[0] + 0.3, cell[1] + 1.0, cell[2] + 0.8 } };
}

/**
 * A mod names the block and the face; the hit aims at the centre of that
 * face of the block's outline.
 */
BreakHit modHit(const std::array<int32_t, 3>& cell)
{
    std::array<double, 3> low { double(cell[0]), double(cell[1]), double(cell[2]) };
    std::array<double, 3> high { cell[0] + 1.0, cell[1] + 1.0, cell[2] + 1.0 };
    return { cell, Up, Stone, faceCentre(low, high, Up) };
}

void modTargetBreaksLikeTheCrosshair()
{
    std::vector<std::pair<bool, std::optional<BreakHit>>> crosshair;
    std::vector<std::pair<bool, std::optional<BreakHit>>> mod;
    for (int i = 0; i < 6; ++i) {
        crosshair.push_back({ true, crosshairHit(Cell) });
        mod.push_back({ true, modHit(Cell) });
    }
    crosshair.push_back({ false, std::nullopt });
    mod.push_back({ false, std::nullopt });
    Trace byHand = run(crosshair, 0.3f);
    Trace byMod = run(mod, 0.3f);
    require(sameActions(byHand, byMod), "A mod's target must send the same actions as the crosshair");
    require(byMod.actions.size() >= 2, "Breaking must start and destroy");
    require(byMod.actions.front().kind == BreakAction::Kind::Start, "The first hit must start breaking");
    require(byMod.actions[1].kind == BreakAction::Kind::Destroy && byMod.actions[1].cell == Cell, "Full progress must destroy the block");
    require(byMod.progress.back().finished, "The last progress of a broken block must say it finished");
}

void stoppingAbortsLikeLettingGo()
{
    std::vector<std::pair<bool, std::optional<BreakHit>>> crosshair { { true, crosshairHit(Cell) }, { true, crosshairHit(Cell) }, { false, std::nullopt } };
    std::vector<std::pair<bool, std::optional<BreakHit>>> mod { { true, modHit(Cell) }, { true, modHit(Cell) }, { false, std::nullopt } };
    Trace byHand = run(crosshair, 0.1f);
    Trace byMod = run(mod, 0.1f);
    require(sameActions(byHand, byMod), "Stopping a mod's break must abort like letting go");
    require(byMod.actions.back().kind == BreakAction::Kind::Abort && byMod.actions.back().cell == Cell, "Stopping must abort the block being broken");
    require(byMod.progress.back().aborted, "The last progress of a stopped block must say it was aborted");
}

void switchingContinuesLikeTheCrosshair()
{
    std::vector<std::pair<bool, std::optional<BreakHit>>> crosshair { { true, crosshairHit(Cell) }, { true, crosshairHit(Other) } };
    std::vector<std::pair<bool, std::optional<BreakHit>>> mod { { true, modHit(Cell) }, { true, modHit(Other) } };
    Trace byHand = run(crosshair, 0.1f);
    Trace byMod = run(mod, 0.1f);
    require(sameActions(byHand, byMod), "Moving a mod's target must switch like sliding the crosshair");
    require(byMod.actions.back().kind == BreakAction::Kind::Continue && byMod.actions.back().cell == Other, "Moving to another block while held must continue on it");
}

void aimsAtTheFaceCentre()
{
    std::array<double, 3> centre = faceCentre({ 0.0, 0.0, 0.0 }, { 1.0, 0.5, 1.0 }, 1);
    require(centre[0] == 0.5 && centre[1] == 0.5 && centre[2] == 0.5, "The top face centre of a slab must sit on its top");
    centre = faceCentre({ 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }, 4);
    require(centre[0] == 0.0 && centre[1] == 0.5 && centre[2] == 0.5, "The west face centre must sit on the low x side");
    std::array<float, 2> south = lookRotation({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 });
    require(std::abs(south[0]) < 1e-4f && std::abs(south[1]) < 1e-4f, "Looking south must be yaw 0 and pitch 0");
    std::array<float, 2> west = lookRotation({ 0.0, 0.0, 0.0 }, { -1.0, 0.0, 0.0 });
    require(std::abs(west[0] - 90.0f) < 1e-4f, "Looking west must be yaw 90");
    std::array<float, 2> down = lookRotation({ 0.0, 1.0, 0.0 }, { 0.0, 0.0, 0.0 });
    require(std::abs(down[1] - 90.0f) < 1e-4f, "Looking straight down must be pitch 90");
}

}

int main()
{
    modTargetBreaksLikeTheCrosshair();
    stoppingAbortsLikeLettingGo();
    switchingContinuesLikeTheCrosshair();
    aimsAtTheFaceCentre();
}
