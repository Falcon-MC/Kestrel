#include "client/ActorTarget.h"

#include <limits>
#include <stdexcept>

using namespace kestrel;

static void check(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

static Tag box(float x, float y, float z, float width = 1.0f, float height = 2.0f)
{
    Tag result = Tag::ofCompound();
    result.putFloat("MinX", -width * 0.5f);
    result.putFloat("MaxX", width * 0.5f);
    result.putFloat("MinY", 0.0f);
    result.putFloat("MaxY", height);
    result.putFloat("MinZ", -width * 0.5f);
    result.putFloat("MaxZ", width * 0.5f);
    result.putFloat("PivotX", x);
    result.putFloat("PivotY", y);
    result.putFloat("PivotZ", z);
    return result;
}

static EntityDataEntry metadata(std::vector<Tag> boxes)
{
    EntityDataEntry result;
    result.mId = HitboxMetadataId;
    result.mFormat = EntityDataFormat::Nbt;
    result.mNbtValue = Tag::ofCompound();
    result.mNbtValue.put("Hitboxes", Tag::ofList(Tag::Type::Compound, std::move(boxes)));
    return result;
}

int main()
{
    ActorView actor;
    actor.identifier = "minecraft:pig";
    actor.x = 10; actor.y = 64; actor.z = 20;
    actor.width = 0.6f; actor.height = 0.9f;
    check(actorTargetDistance(actor, {10, 64.5, 18}, {0, 0, 1}, 3, 0.1).has_value(), "normal actor fallback missing");
    applyActorHitboxes(metadata({box(-2, 4, 0), box(2, 4, 0)}), actor.hitboxes);
    check(actor.hitboxes && actor.hitboxes->size() == 2, "multiple boxes not retained");
    check(!actorTargetDistance(actor, {10, 64.5, 18}, {0, 0, 1}, 3, 0.1), "custom boxes must replace the collision targeting box");
    check(actorTargetDistance(actor, {8, 68, 18}, {0, 0, 1}, 3, 0.1).has_value(), "raised box cannot be picked");
    check(!actorTargetDistance(actor, {10, 68, 18}, {0, 0, 1}, 3, 0.1), "gap between disjoint boxes became targetable");
    actor.yaw = 90; actor.scale = 4;
    check(actorTargetDistance(actor, {8, 68, 18}, {0, 0, 1}, 3, 0.1).has_value(), "custom pivot must not rotate or scale");
    actor.scale = 0;
    check(actorTargetDistance(actor, {8, 68, 18}, {0, 0, 1}, 3, 0.1).has_value(), "zero collision scale hid custom boxes");
    auto published = actor.hitboxes;
    applyActorHitboxes(metadata({box(0, 6, 0)}), actor.hitboxes);
    check(actor.hitboxes->size() == 3 && published->size() == 2, "update mutated an existing snapshot or replaced its boxes");
    auto saved = actor.hitboxes;
    applyActorHitboxes(metadata({}), actor.hitboxes);
    auto wrong = metadata({});
    wrong.mNbtValue = Tag::ofString("bad");
    applyActorHitboxes(wrong, actor.hitboxes);
    wrong.mNbtValue = Tag::ofCompound();
    applyActorHitboxes(wrong, actor.hitboxes);
    wrong = metadata({}); wrong.mFormat = EntityDataFormat::Int;
    applyActorHitboxes(wrong, actor.hitboxes);
    check(saved == actor.hitboxes, "empty or malformed update cleared existing state");
    actor.hitboxes.reset();
    Tag reversed = box(0, 1, 0);
    reversed.putFloat("MinX", 2); reversed.putFloat("MaxX", -2);
    applyActorHitboxes(metadata({reversed}), actor.hitboxes);
    check(actor.hitboxes->front().half[0] == 2, "reversed extents not normalized");
    Tag invalid = box(0, 1, 0);
    invalid.putFloat("PivotY", std::numeric_limits<float>::infinity());
    applyActorHitboxes(metadata({invalid, box(0, 2, 0)}), actor.hitboxes);
    check(actor.hitboxes->size() == 2, "invalid entry discarded a valid sibling");
    ActorHitboxList defaults;
    Tag missing = Tag::ofCompound();
    missing.putInt("PivotX", 100);
    applyActorHitboxes(metadata({missing}), defaults);
    check(defaults && defaults->front().pivot[0] == 0 && defaults->front().half[0] == 0, "fields must default to zero without numeric coercion");
    actor.hitboxes.reset();
    std::vector<Tag> many(130, box(0, 1, 0));
    applyActorHitboxes(metadata(std::move(many)), actor.hitboxes);
    check(actor.hitboxes->size() == 130, "box list truncated at 64 or candidate limit");
    applyActorHitboxes(metadata(std::vector<Tag>(MaxActorHitboxes, box(0, 1, 0))), actor.hitboxes);
    check(actor.hitboxes->size() == MaxActorHitboxes, "server-controlled storage is not bounded");
    actor.hitboxes.reset();
    applyActorHitboxes(metadata({box(0, 1, -4), box(0, 1, 5), box(0, 1, 0), box(0, 1, -1)}), actor.hitboxes);
    actor.x = 0; actor.y = 0; actor.z = 0;
    auto ordered = actorTargetDistance(actor, {0, 1, -3}, {0, 0, 1}, 3, 0);
    check(ordered && *ordered == 2.5, "first intersected in-range box must win in server order");
    actor.identifier = "minecraft:player";
    bool nativeOrigin = false;
    visitActorTargetBoxes(actor, {3, 10, 7}, [&](const ActorTargetBox& hit) {
        nativeOrigin = std::abs(hit.low[1] - 11.62) < 0.000001;
        return false;
    });
    check(nativeOrigin, "player eye-origin offset or explicit predicted position lost");
    for (size_t axis = 0; axis < 3; ++axis) {
        auto invalidFeet = std::array<double, 3> { 3, 10, 7 };
        invalidFeet[axis] = std::numeric_limits<double>::quiet_NaN();
        bool visited = false;
        visitActorTargetBoxes(actor, invalidFeet, [&](const ActorTargetBox&) {
            visited = true;
            return true;
        });
        check(!visited, "Invalid actor coordinates must not reach selection or mod attack visitors");
    }
    actor.hitboxes.reset();
    actor.x = actor.y = actor.z = std::numeric_limits<double>::quiet_NaN();
    check(!actorTargetDistance(actor, {0, 1.62, 0}, {0, 0, 1}, 3, 0.1), "NaN fallback actor intercepted the ray");
    actor.x = actor.y = actor.z = 0;
    actor.width = actor.height = actor.scale = std::numeric_limits<float>::max();
    check(!actorTargetDistance(actor, {0, 1.62, 0}, {0, 0, 1}, 3, 0.1), "Overflowed actor extents intercepted the ray");
    actor.identifier = "minecraft:pig";
    const ActorHitbox validBox { {0, 1, 0}, {0.5, 1, 0.5} };
    auto invalidBox = validBox;
    invalidBox.pivot[0] = std::numeric_limits<double>::quiet_NaN();
    actor.hitboxes = std::make_shared<const std::vector<ActorHitbox>>(std::vector<ActorHitbox> { invalidBox, validBox });
    auto validSibling = actorTargetDistance(actor, {0, 1, -2}, {0, 0, 1}, 3, 0.1);
    check(validSibling && std::abs(*validSibling - 1.4) < 1e-6, "Invalid custom bounds must not hide a valid sibling");
}
