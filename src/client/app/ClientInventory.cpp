#include "client/Client.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kestrel {

void Client::drawInventoryEntity(ui::Context& ui, const std::string& identifier, const ui::Rect& rect, float alpha)
{
    const auto* model = blockAssets ? blockAssets->entityModel(identifier) : nullptr;
    if (!model || model->rigs.empty() || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto& rig = model->rigs.front();
    const auto& pixels = blockAssets->entityTexturePixels();
    constexpr size_t pageSize = size_t(world::EntityTextureSize) * world::EntityTextureSize * 4;
    size_t offset = size_t(model->layer) * pageSize;
    if (offset + pageSize > pixels.size()) {
        return;
    }
    std::string texture = "inventory/entity/" + identifier;
    if (!ui.skin().sprite(texture).valid) {
        ui.skin().setDynamic(texture, { world::EntityTextureSize, world::EntityTextureSize,
            std::vector<uint8_t>(pixels.begin() + offset, pixels.begin() + offset + pageSize) });
    }
    float yaw = 0.65f + std::atan((ui.mouseX() - rect.x - rect.w * 0.5f) / 100.0f) * 0.25f;
    float pitch = 0.15f + std::atan((ui.mouseY() - rect.y - rect.h * 0.5f) / 100.0f) * 0.15f;
    auto project = [&](const std::array<int16_t, 3>& point) {
        float x = point[0] * std::cos(yaw) + point[2] * std::sin(yaw);
        float z = point[2] * std::cos(yaw) - point[0] * std::sin(yaw);
        float y = point[1] * std::cos(pitch) - z * std::sin(pitch);
        return std::array<float, 3> { x, -y, z * std::cos(pitch) + point[1] * std::sin(pitch) };
    };
    struct Face {
        std::array<std::array<float, 3>, 4> points;
        std::array<std::array<float, 2>, 4> uv;
        float depth = 0.0f;
    };
    std::vector<Face> faces;
    float left = std::numeric_limits<float>::max();
    float top = left;
    float right = -left;
    float bottom = -left;
    for (const auto& quad : rig.quads) {
        Face face;
        for (size_t corner = 0; corner < 4; ++corner) {
            face.points[corner] = project(quad.positions[corner]);
            face.uv[corner] = { quad.uvs[corner][0] * world::EntityTextureSize / 4096.0f,
                quad.uvs[corner][1] * world::EntityTextureSize / 4096.0f };
            face.depth += face.points[corner][2] * 0.25f;
            left = std::min(left, face.points[corner][0]);
            right = std::max(right, face.points[corner][0]);
            top = std::min(top, face.points[corner][1]);
            bottom = std::max(bottom, face.points[corner][1]);
        }
        faces.push_back(face);
    }
    if (faces.empty()) {
        return;
    }
    std::stable_sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) {
        return a.depth > b.depth;
    });
    float scale = std::min(rect.w / std::max(1.0f, right - left), rect.h / std::max(1.0f, bottom - top)) * 0.9f;
    ui::Color tint { 255, 255, 255, uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) };
    for (const auto& face : faces) {
        std::array<std::array<float, 2>, 4> points;
        for (size_t corner = 0; corner < 4; ++corner) {
            points[corner] = { rect.x + rect.w * 0.5f + (face.points[corner][0] - (left + right) * 0.5f) * scale,
                rect.y + rect.h * 0.5f + (face.points[corner][1] - (top + bottom) * 0.5f) * scale };
        }
        ui.spriteQuad(points, texture, face.uv, tint);
    }
}

}
