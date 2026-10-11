#include "client/AimAssist.h"

#include "Protocol/Packets/CameraAimAssistPacket.h"
#include "Protocol/Packets/CameraAimAssistPresetsPacket.h"
#include "Protocol/Packets/CameraAimAssistActorPriorityPacket.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace kestrel {
namespace {

constexpr size_t MaxDefinitions = 4096;
constexpr size_t MaxRules = 16384;

size_t ruleCount(const CameraAimAssistCategory& value)
{
    size_t count = 1;
    for (const auto* list : {&value.mActorPriorities, &value.mBlockPriorities, &value.mBlockTagPriorities, &value.mActorTypeFamiliesPriorities}) {
        for (const auto& rule : *list) {
            if (rule.mName.size() > 256 || ++count > MaxRules) return MaxRules + 1;
        }
    }
    return count;
}

size_t ruleCount(const CameraAimAssistPresetDefinition& value)
{
    size_t count = 1;
    for (const auto* list : {&value.mBlockExclusionList, &value.mActorExclusionList, &value.mBlockTagExclusionList,
             &value.mActorTypeFamiliesExclusionList, &value.mLiquidTargetingList}) {
        for (const auto& rule : *list) {
            if (rule.size() > 256 || ++count > MaxRules) return MaxRules + 1;
        }
    }
    for (const auto& rule : value.mItemSettings) {
        if (rule.mItemId.size() > 256 || rule.mCategory.size() > 256 || ++count > MaxRules) return MaxRules + 1;
    }
    return value.mDefaultItemSettings.size() <= 256 && value.mHandSettings.size() <= 256 ? count : MaxRules + 1;
}

bool contains(const std::vector<std::string>& names, std::string_view name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

int priority(const std::vector<CameraAimAssistPriority>& values, std::string_view name, int fallback)
{
    auto value = std::find_if(values.begin(), values.end(), [&](const auto& entry) { return entry.mName == name; });
    return value == values.end() ? fallback : std::clamp(value->mPriority, 0, 100);
}

template<class T, class Name>
void mergeDefinitions(std::vector<T>& stored, const std::vector<T>& incoming, Name name)
{
    size_t rules = 0;
    for (const T& entry : stored) rules += ruleCount(entry);
    for (const T& entry : incoming) {
        if (name(entry).size() > 256) continue;
        auto previous = std::find_if(stored.begin(), stored.end(), [&](const T& value) { return name(value) == name(entry); });
        size_t oldCount = previous == stored.end() ? 0 : ruleCount(*previous);
        size_t newCount = ruleCount(entry);
        if (rules - oldCount + newCount > MaxRules) continue;
        if (previous == stored.end() && stored.size() >= MaxDefinitions) continue;
        rules = rules - oldCount + newCount;
        if (previous != stored.end()) *previous = entry;
        else stored.push_back(entry);
    }
}

}

void AimAssist::reset(bool keepPresets)
{
    active = false;
    presetId.clear();
    if (!keepPresets) {
        categories.clear(); presets.clear(); actorPriorities.clear();
    }
}

void AimAssist::apply(const std::vector<std::shared_ptr<const Packet>>& events)
{
    for (const auto& event : events) {
        if (auto command = dynamic_cast<const CameraAimAssistPacket*>(event.get())) {
            active = false;
            if (command->mAction != AimAssistAction::Set) continue;
            if (!std::isfinite(command->mViewAngle.x) || !std::isfinite(command->mViewAngle.y)
                || !std::isfinite(command->mDistance) || command->mDistance < 1.0f || command->mDistance > 16.0f
                || command->mViewAngle.x < 10.0f || command->mViewAngle.x > 90.0f
                || command->mViewAngle.y < 10.0f || command->mViewAngle.y > 90.0f
                || (command->mTargetMode != CameraAimAssistPacket::TargetMode::Angle
                    && command->mTargetMode != CameraAimAssistPacket::TargetMode::Distance)) continue;
            angleX = command->mViewAngle.x; angleY = command->mViewAngle.y;
            distance = command->mDistance;
            distanceMode = command->mTargetMode == CameraAimAssistPacket::TargetMode::Distance;
            presetId = command->mPresetId;
            active = true;
        } else if (auto definitions = dynamic_cast<const CameraAimAssistPresetsPacket*>(event.get())) {
            if (definitions->mOperation == CameraAimAssistOperation::Set) {
                categories.clear(); presets.clear(); actorPriorities.clear();
            } else if (definitions->mOperation != CameraAimAssistOperation::AddToExisting) continue;
            mergeDefinitions(categories, definitions->mCategoryDefinitions, [](const auto& entry) -> const std::string& { return entry.mName; });
            mergeDefinitions(presets, definitions->mPresets, [](const auto& entry) -> const std::string& { return entry.mIdentifier; });
        } else if (auto priorities = dynamic_cast<const CameraAimAssistActorPriorityPacket*>(event.get())) {
            for (const auto& value : priorities->mPriorityData) {
                if (value.mPresetIndex < 0 || value.mCategoryIndex < 0 || value.mActorIndex < 0) continue;
                auto key = std::tuple(value.mPresetIndex, value.mCategoryIndex, value.mActorIndex);
                if (actorPriorities.size() < MaxDefinitions * 16 || actorPriorities.contains(key))
                    actorPriorities[key] = std::clamp(value.mPriorityValue, -2, 100);
            }
        }
    }
}

std::optional<AimAssistTarget> AimAssist::select(const AimAssistContext& context) const
{
    if (!active || !std::isfinite(context.yaw) || !std::isfinite(context.pitch)) return {};
    for (double coordinate : context.origin) {
        if (!std::isfinite(coordinate) || coordinate < std::numeric_limits<int32_t>::min() + 32.0
            || coordinate > std::numeric_limits<int32_t>::max() - 32.0) return {};
    }
    const CameraAimAssistPresetDefinition* preset = nullptr;
    const CameraAimAssistCategory* category = nullptr;
    if (!presetId.empty()) {
        auto found = std::find_if(presets.begin(), presets.end(), [&](const auto& value) { return value.mIdentifier == presetId; });
        if (found == presets.end()) return {};
        preset = &*found;
        std::string_view categoryName;
        auto item = std::find_if(preset->mItemSettings.begin(), preset->mItemSettings.end(), [&](const auto& entry) { return entry.mItemId == context.item; });
        if (item != preset->mItemSettings.end()) categoryName = item->mCategory;
        else if (context.item.empty() && preset->mHasHandSettings) categoryName = preset->mHandSettings;
        else if (preset->mHasDefaultItemSettings) categoryName = preset->mDefaultItemSettings;
        auto rule = std::find_if(categories.begin(), categories.end(), [&](const auto& value) { return value.mName == categoryName; });
        if (rule == categories.end()) return {};
        category = &*rule;
    }

    constexpr double ToDegrees = 180.0 / std::numbers::pi;
    double bestScore = std::numeric_limits<double>::infinity();
    std::optional<AimAssistTarget> target;
    auto score = [&](const std::array<double, 3>& point, int weight) -> std::optional<double> {
        if (weight <= 0) return {};
        double x = point[0] - context.origin[0], y = point[1] - context.origin[1], z = point[2] - context.origin[2];
        double range = std::sqrt(x * x + y * y + z * z);
        if (!std::isfinite(range) || range <= 0.0001 || range > distance) return {};
        double horizontal = std::remainder(std::atan2(-x, z) * ToDegrees - context.yaw, 360.0);
        double vertical = -std::atan2(y, std::hypot(x, z)) * ToDegrees - context.pitch;
        double cone = std::hypot(horizontal / angleX, vertical / angleY);
        if (cone > 1.0) return {};
        return (distanceMode ? range : cone) / weight;
    };
    auto visible = [&](const AimAssistTarget& candidate) {
        std::array<double, 3> ray;
        double length = 0.0;
        for (size_t axis = 0; axis < 3; ++axis) {
            ray[axis] = candidate.point[axis] - context.origin[axis];
            length += ray[axis] * ray[axis];
        }
        length = std::sqrt(length);
        for (double& component : ray) component /= length;
        for (const ActorView& actor : context.actors) {
            if (actor.runtimeId == context.localRuntimeId || (candidate.entity && actor.runtimeId == candidate.runtimeId)) continue;
            bool blocked = false;
            visitActorTargetBoxes(actor, {actor.x, actor.y, actor.z}, [&](const ActorTargetBox& box) {
                auto entry = enterBox(context.origin, ray, box.low, box.high);
                blocked = entry && *entry > 0.0001 && *entry < length - 0.0001;
                return !blocked;
            });
            if (blocked) return false;
        }
        if (!context.block) return true;
        std::array<int32_t, 3> cell, step;
        std::array<double, 3> next, delta;
        for (size_t axis = 0; axis < 3; ++axis) {
            cell[axis] = int32_t(std::floor(context.origin[axis]));
            step[axis] = ray[axis] > 0 ? 1 : ray[axis] < 0 ? -1 : 0;
            delta[axis] = step[axis] ? std::abs(1.0 / ray[axis]) : std::numeric_limits<double>::infinity();
            next[axis] = step[axis] ? (ray[axis] > 0 ? cell[axis] + 1.0 - context.origin[axis] : context.origin[axis] - cell[axis]) * delta[axis] : delta[axis];
        }
        for (double travelled = 0; travelled < length;) {
            if (auto block = context.block(cell); block && !block->liquid && (candidate.entity || cell != candidate.cell)) {
                auto entry = enterBox(context.origin, ray, block->box.low, block->box.high);
                if (entry && *entry < length - 0.0001) return false;
            }
            size_t axis = size_t(std::min_element(next.begin(), next.end()) - next.begin());
            travelled = next[axis]; next[axis] += delta[axis]; cell[axis] += step[axis];
        }
        return true;
    };
    auto consider = [&](const ActorTargetBox& box, AimAssistTarget candidate, int weight) {
        for (size_t axis = 0; axis < 3; ++axis) candidate.point[axis] = (box.low[axis] + box.high[axis]) * 0.5;
        auto value = score(candidate.point, weight);
        if (value && *value < bestScore && visible(candidate)) { bestScore = *value; target = candidate; }
    };
    size_t admitted = 0;
    for (const ActorView& actor : context.actors) {
        if (actor.runtimeId == context.localRuntimeId || (preset && contains(preset->mActorExclusionList, actor.identifier))) continue;
        int weight = category ? priority(category->mActorPriorities, actor.identifier,
            category->mHasActorDefaultPriorities ? std::clamp(category->mActorDefaultPriorities, 0, 100) : 1) : 1;
        int32_t presetIndex = context.localIndices[0], categoryIndex = context.localIndices[1], actorIndex = actor.aimAssistIndices[2];
        if (presetIndex >= 0 && categoryIndex >= 0 && actorIndex >= 0) {
            auto exclusion = actorPriorities.find({presetIndex, 0, actorIndex});
            // Category zero carries server-resolved exclusions for non-player actors.
            if (actor.identifier != "minecraft:player" && exclusion != actorPriorities.end() && exclusion->second == -2) continue;
            auto override = actorPriorities.find({presetIndex, categoryIndex, actorIndex});
            weight = override == actorPriorities.end() ? 0 : override->second;
        } else if (category && (!category->mActorTypeFamiliesPriorities.empty() || !preset->mActorTypeFamiliesExclusionList.empty())) {
            weight = 0;
        }
        if (weight <= 0) continue;
        if (admitted++ >= 128) break;
        visitActorTargetBoxes(actor, {actor.x, actor.y, actor.z}, [&](const ActorTargetBox& box) {
            consider(box, AimAssistTarget { {}, actor.runtimeId, {}, true }, weight);
            return true;
        });
    }
    if (!context.block || bestScore == 0.0) return target;
    bool liquids = preset && contains(preset->mLiquidTargetingList, context.item);
    int radius = int(std::ceil(distance));
    std::array<int32_t, 3> center {int32_t(std::floor(context.origin[0])), int32_t(std::floor(context.origin[1])), int32_t(std::floor(context.origin[2]))};
    double yawRadians = context.yaw / ToDegrees, pitchRadians = context.pitch / ToDegrees;
    double forwardX = -std::sin(yawRadians), forwardZ = std::cos(yawRadians);
    double pitchCos = std::cos(pitchRadians), pitchSin = std::sin(pitchRadians);
    double horizontalCos = std::cos(angleX / ToDegrees), verticalCos = std::cos(angleY / ToDegrees);
    for (int y = -radius; y <= radius; ++y) for (int z = -radius; z <= radius; ++z) for (int x = -radius; x <= radius; ++x) {
        std::array<int32_t, 3> cell {center[0] + x, center[1] + y, center[2] + z};
        double dx = cell[0] + 0.5 - context.origin[0], dy = cell[1] + 0.5 - context.origin[1], dz = cell[2] + 0.5 - context.origin[2];
        double horizontalSquared = dx * dx + dz * dz, rangeSquared = horizontalSquared + dy * dy;
        if (rangeSquared > distance * distance) continue;
        double horizontalDot = dx * forwardX + dz * forwardZ;
        if (horizontalDot < 0 || horizontalDot * horizontalDot + 1.0e-12 < horizontalSquared * horizontalCos * horizontalCos) continue;
        double verticalDot = std::sqrt(horizontalSquared) * pitchCos - dy * pitchSin;
        if (verticalDot < 0 || verticalDot * verticalDot + 1.0e-12 < rangeSquared * verticalCos * verticalCos) continue;
        if (!score({cell[0] + 0.5, cell[1] + 0.5, cell[2] + 0.5}, 100)) continue;
        auto block = context.block(cell);
        if (!block || block->name.empty() || (block->liquid && !liquids) || (preset && contains(preset->mBlockExclusionList, block->name))) continue;
        if (preset && std::any_of(block->tags.begin(), block->tags.end(), [&](const auto& tag) { return contains(preset->mBlockTagExclusionList, tag); })) continue;
        int weight = category ? priority(category->mBlockPriorities, block->name,
            category->mHasBlockDefaultPriorities ? std::clamp(category->mBlockDefaultPriorities, 0, 100) : 1) : 1;
        if (category) for (const auto& tag : block->tags) weight = priority(category->mBlockTagPriorities, tag, weight);
        consider(block->box, AimAssistTarget { {}, 0, cell, false }, weight);
    }
    return target;
}

}
