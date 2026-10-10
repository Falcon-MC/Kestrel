#include "client/AimAssist.h"
#include "client/ActorAimAssist.h"
#include "client/AimAssistRay.h"
#include "world/BlockTags.h"
#include "Protocol/Packets/CameraAimAssistPacket.h"
#include "Protocol/Packets/CameraAimAssistPresetsPacket.h"
#include "Protocol/Packets/CameraAimAssistActorPriorityPacket.h"

#include <limits>
#include <stdexcept>

using namespace kestrel;

static void check(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

static std::shared_ptr<CameraAimAssistPacket> command()
{
    auto packet = std::make_shared<CameraAimAssistPacket>();
    packet->mViewAngle = {30, 30};
    packet->mDistance = 10;
    return packet;
}

static ActorView actor(uint64_t id, double x, double z)
{
    ActorView result;
    result.runtimeId = id; result.identifier = "minecraft:pig";
    result.x = x; result.z = z; result.y = 0;
    result.hitboxes = std::make_shared<const std::vector<ActorHitbox>>(std::vector<ActorHitbox> {
        {{-1, 1, 0}, {0.4, 0.5, 0.4}}, {{1, 1, 0}, {0.4, 0.5, 0.4}}});
    return result;
}

int main()
{
    AimAssist aim;
    std::vector<ActorView> actors {actor(1, 0, 3)};
    AimAssistContext context;
    context.origin = {0, 1, 0}; context.actors = actors;
    check(!aim.select(context), "aim assist enabled without a server instruction");
    auto set = command();
    aim.apply({set});
    auto target = aim.select(context);
    check(target && target->entity && target->runtimeId == 1 && target->point[0] == -1, "boxes were replaced by their union or lost server order");
    context.block = [](const auto& cell) -> std::optional<AimAssistBlock> {
        if (cell != std::array<int32_t, 3>{-1, 1, 1}) return {};
        return AimAssistBlock {{{-1, 1, 1}, {0, 2, 2}}, "minecraft:stone"};
    };
    auto definitions = std::make_shared<CameraAimAssistPresetsPacket>();
    CameraAimAssistCategory category;
    category.mName = "audit:entities";
    category.mHasBlockDefaultPriorities = true; category.mBlockDefaultPriorities = 0;
    definitions->mCategoryDefinitions.push_back(category);
    CameraAimAssistPresetDefinition preset;
    preset.mIdentifier = "audit:test";
    preset.mHasHandSettings = true; preset.mHandSettings = category.mName;
    preset.mHasDefaultItemSettings = true; preset.mDefaultItemSettings = category.mName;
    definitions->mPresets.push_back(preset);
    set->mPresetId = preset.mIdentifier;
    aim.apply({definitions, set});
    target = aim.select(context);
    check(target && target->point[0] == 1, "occlusion of one box hid another visible box");
    context.block = {};
    actors[0].yaw = 90; actors[0].scale = 10;
    check(aim.select(context)->point[0] == -1, "aim assist rotated or scaled a custom hitbox");
    context.yaw = 180;
    check(!aim.select(context), "targeting did not follow the player's orientation");
    context.yaw = 0;
    auto many = std::make_shared<std::vector<ActorHitbox>>(129, ActorHitbox{{5, 1, 0}, {0.1, 0.1, 0.1}});
    many->push_back({{0, 1, 0}, {0.1, 0.1, 0.1}});
    actors[0].hitboxes = many;
    check(aim.select(context)->point[0] == 0, "last box of a large admitted actor was discarded");
    actors[0] = actor(1, 0, 3);
    actors.push_back(actor(2, 1, 6));
    context.actors = actors;
    set->mTargetMode = CameraAimAssistPacket::TargetMode::Distance;
    aim.apply({set});
    check(aim.select(context)->runtimeId == 1, "distance mode ignored the closest target");
    set->mTargetMode = CameraAimAssistPacket::TargetMode::Angle;
    aim.apply({set});
    check(aim.select(context)->runtimeId == 2, "angle mode ignored the centered target");
    auto priorities = std::make_shared<CameraAimAssistActorPriorityPacket>();
    context.localIndices = {4, 6, -1};
    actors[0].aimAssistIndices = {0, 0, 7};
    actors[1].aimAssistIndices = {0, 0, 8};
    priorities->mPriorityData = {{4, 6, 7, 10}, {4, 6, 8, 0}};
    aim.apply({priorities});
    check(aim.select(context)->runtimeId == 1, "resolved actor priorities did not use metadata actor indices");
    auto excluded = std::make_shared<CameraAimAssistActorPriorityPacket>();
    excluded->mPriorityData = {{4, 0, 7, -2}};
    aim.apply({excluded});
    check(!aim.select(context), "server exclusion sentinel was ignored");
    excluded->mPriorityData = {{4, 0, 7, 0}};
    aim.apply({excluded});
    EntityDataEntry metadata;
    metadata.mId = 138; metadata.mFormat = EntityDataFormat::Int; metadata.mIntValue = 9;
    applyActorAimAssist(metadata, actors[1].aimAssistIndices);
    check(actors[1].aimAssistIndices == std::array<int32_t, 3>{0, 0, 9}, "actor index update overwrote other priority indices");
    metadata.mFormat = EntityDataFormat::Float;
    metadata.mIntValue = 7;
    applyActorAimAssist(metadata, actors[1].aimAssistIndices);
    check(actors[1].aimAssistIndices[2] == 9, "wrong metadata format changed a priority index");
    actors[0].aimAssistIndices = actors[1].aimAssistIndices = {-1, -1, -1};
    definitions->mCategoryDefinitions[0].mActorTypeFamiliesPriorities.push_back({"audit", 10});
    aim.apply({definitions});
    check(!aim.select(context), "unresolved entity families bypassed server priorities");
    actors[0].aimAssistIndices[2] = 7;
    aim.apply({priorities});
    check(aim.select(context)->runtimeId == 1, "resolved entity family priority was ignored");
    definitions->mCategoryDefinitions[0].mActorTypeFamiliesPriorities.clear();
    actors[0].aimAssistIndices[2] = -1;
    context.localIndices = {-1, -1, -1};
    aim.apply({definitions});
    std::array<float, 3> ray {0, 0, 1};
    check(aimAssistDirection({0, 1, 0}, std::array<double, 3>{3, 1, 4}, ray)
        && std::abs(ray[0] - 0.6f) < 0.0001f && std::abs(ray[2] - 0.8f) < 0.0001f,
        "interaction ray did not point at the assisted target");
    check(!aimAssistDirection({0, 1, 0}, {}, ray) && std::abs(ray[0] - 0.6f) < 0.0001f,
        "clearing assist changed the regular interaction ray");
    check(!aimAssistDirection({0, 1, 0}, std::array<double, 3>{0, 1, 0}, ray), "zero-length assist direction accepted");
    definitions->mPresets[0].mActorExclusionList = {"minecraft:pig"};
    aim.apply({definitions});
    check(!aim.select(context), "preset entity exclusions were ignored");
    definitions->mPresets[0].mActorExclusionList.clear();
    CameraAimAssistCategory blocks;
    blocks.mName = "audit:blocks";
    blocks.mHasActorDefaultPriorities = true; blocks.mActorDefaultPriorities = 0;
    blocks.mHasBlockDefaultPriorities = true; blocks.mBlockDefaultPriorities = 0;
    blocks.mBlockPriorities.push_back({"minecraft:diamond_block", 6});
    definitions->mCategoryDefinitions.push_back(blocks);
    definitions->mPresets[0].mItemSettings.push_back({"minecraft:stick", blocks.mName});
    context.block = [](const auto& cell) -> std::optional<AimAssistBlock> {
        if (cell != std::array<int32_t, 3>{-1, 1, 4}) return {};
        return AimAssistBlock {{{-1, 1, 4}, {0, 2, 5}}, "minecraft:diamond_block"};
    };
    aim.apply({definitions});
    context.item = "minecraft:stick";
    target = aim.select(context);
    check(target && !target->entity && target->cell == std::array<int32_t, 3>{-1, 1, 4}, "item-specific block category was ignored");
    context.block = [](const auto& cell) -> std::optional<AimAssistBlock> {
        if (cell != std::array<int32_t, 3>{-1, 1, 4}) return {};
        return AimAssistBlock {{{-1, 1, 4}, {0, 2, 5}}, "minecraft:water", {}, true};
    };
    definitions->mCategoryDefinitions[1].mBlockPriorities.push_back({"minecraft:water", 6});
    aim.apply({definitions});
    check(!aim.select(context), "liquids targeted without an allowed item");
    definitions->mPresets[0].mLiquidTargetingList.push_back("minecraft:stick");
    aim.apply({definitions});
    check(aim.select(context).has_value(), "liquid targeting item was ignored");
    Tag blockDefinition = Tag::ofCompound();
    blockDefinition.put("blockTags", Tag::ofList(Tag::Type::String, {Tag::ofString("audit:target")}));
    auto tags = world::readBlockTags(blockDefinition);
    check(tags == std::vector<std::string>{"audit:target"}, "server block tags were not read");
    definitions->mCategoryDefinitions[1].mBlockPriorities.clear();
    definitions->mCategoryDefinitions[1].mBlockTagPriorities.push_back({"audit:target", 10});
    context.block = [&](const auto& cell) -> std::optional<AimAssistBlock> {
        if (cell != std::array<int32_t, 3>{-1, 1, 4}) return {};
        return AimAssistBlock {{{-1, 1, 4}, {0, 2, 5}}, "audit:block", tags};
    };
    aim.apply({definitions});
    check(aim.select(context).has_value(), "block tag category did not select a tagged block");
    definitions->mPresets[0].mBlockTagExclusionList.push_back("audit:target");
    aim.apply({definitions});
    check(!aim.select(context), "block tag exclusion was ignored");
    definitions->mPresets[0].mBlockTagExclusionList.clear();
    aim.apply({definitions});
    set->mAction = AimAssistAction::Clear;
    aim.apply({set});
    check(!aim.enabled() && !aim.select(context), "clear instruction left a stale target");
    set->mAction = AimAssistAction::Set;
    set->mDistance = std::numeric_limits<float>::infinity();
    aim.apply({set});
    check(!aim.enabled(), "non-finite server settings accepted");
    set->mDistance = 10;
    aim.apply({set});
    aim.reset(true);
    check(!aim.enabled(), "dimension reset kept an active target");
    aim.apply({set});
    check(aim.select(context).has_value(), "dimension reset discarded preset definitions");
    aim.reset();
    aim.apply({set});
    check(!aim.select(context), "new session reused old preset definitions");
}
