#include "client/Client.h"
#include "platform/Window.h"

#include <cmath>

namespace kestrel {

void Client::updateAimAssist()
{
    aimTarget.reset();
    if (!aimAssist.enabled() || !playerView.active || (perspective == PerspectiveFirst && !cameraDetached)) {
        session.setAimAssistTarget({});
        return;
    }
    const auto& blocks = seenSessionSnapshot->loaded;
    if (!blocks || !blocks->assets) {
        session.setAimAssistTarget({});
        return;
    }
    if (aimBlockAssets != blocks->assets.get() || aimBlockIds != blocks->ids.sequential.get()) {
        aimBlockNames.clear();
        aimBlockAssets = blocks->assets.get(); aimBlockIds = blocks->ids.sequential.get();
    }
    AimAssistContext input;
    input.origin = eyePosition;
    input.yaw = localLookYaw; input.pitch = localLookPitch;
    input.localRuntimeId = localRuntime;
    input.localIndices = seenSessionSnapshot->localAimAssistIndices;
    input.actors = actorViews;
    input.item = hudState.inventory[size_t(std::clamp(hudState.selectedSlot, 0, 8))].identifier;
    input.block = [&](const std::array<int32_t, 3>& cell) -> std::optional<AimAssistBlock> {
        ActorTargetBox cube { {double(cell[0]), double(cell[1]), double(cell[2])},
            {cell[0] + 1.0, cell[1] + 1.0, cell[2] + 1.0} };
        if (!blocks->loaded(cell[0], cell[1], cell[2])) return AimAssistBlock {cube, {}, {}, false};
        uint32_t value = blocks->value(cell[0], cell[1], cell[2]);
        if (value == world::ImplicitAir || (blocks->ids.hidden && blocks->ids.hidden->contains(value))) return {};
        const auto& visual = blocks->assets->visual(value, blocks->ids.hashed, blocks->ids.sequential.get());
        if (visual.flags & world::FlagAir) return {};
        auto name = aimBlockNames.find(value);
        if (name == aimBlockNames.end()) {
            if (aimBlockNames.size() >= 65536) return {};
            name = aimBlockNames.emplace(value, blocks->name(cell[0], cell[1], cell[2])).first;
        }
        if (name->second == "minecraft:fire" || name->second == "minecraft:soul_fire") return {};
        if (!visual.liquid) {
            auto outline = blocks->outline(cell[0], cell[1], cell[2]);
            if (!outline) return {};
            cube = { {outline->minX, outline->minY, outline->minZ}, {outline->maxX, outline->maxY, outline->maxZ} };
        }
        return AimAssistBlock {cube, name->second, blocks->assets->blockTags(value, blocks->ids.hashed, blocks->ids.sequential.get()), visual.liquid != 0};
    };
    aimTarget = aimAssist.select(input);
    session.setAimAssistTarget(aimTarget ? std::optional(aimTarget->point) : std::nullopt);
    if (aimTarget) skin.sprite(aimTarget->entity ? "ui/aimassist_entity_highlight" : "ui/aimassist_block_highlight");
}

void Client::drawAimAssist(ui::Context& context, float scale)
{
    if (!aimTarget) return;
    float width = window->width() / scale, height = window->height() / scale;
    Mat4 matrix = camera.viewProjection(width / std::max(height, 1.0f));
    std::array<double, 3> position {aimTarget->point[0] - camera.x(), aimTarget->point[1] - camera.y(), aimTarget->point[2] - camera.z()};
    auto row = [&](size_t r) { return matrix[r] * position[0] + matrix[4 + r] * position[1] + matrix[8 + r] * position[2] + matrix[12 + r]; };
    double w = row(3), depth = row(2);
    if (!std::isfinite(w) || w <= 0.05 || depth < 0 || depth > w) return;
    float x = float((row(0) / w + 1.0) * width * 0.5);
    float y = float((1.0 - row(1) / w) * height * 0.5);
    std::string_view texture = aimTarget->entity ? "ui/aimassist_entity_highlight" : "ui/aimassist_block_highlight";
    const auto& sprite = skin.sprite(texture);
    if (sprite.valid) context.sprite({x - sprite.width * 0.5f, y - sprite.height * 0.5f, sprite.width, sprite.height}, texture);
}

}
