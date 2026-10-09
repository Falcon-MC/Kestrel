#include "client/Client.h"
#include "modding/Painters.h"

#include <algorithm>

namespace kestrel {

void Client::drawSignTexts(modding::WorldCanvas& painter)
{
    for (const auto& sign : signTextViews) {
        const auto pose = signTextPose(sign.name, sign.rotation, sign.facing, sign.hanging);
        for (size_t side = 0; side < sign.sides.size(); ++side) {
            const SignText& data = sign.sides[side];
            if (data.text.empty()) continue;
            const float direction = side == 0 ? 1.0f : -1.0f;
            const mod::Vec3 center {
                sign.cell[0] + pose.center[0] + pose.normal[0] * pose.depth * direction,
                sign.cell[1] + pose.center[1],
                sign.cell[2] + pose.center[2] + pose.normal[2] * pose.depth * direction,
            };
            const double facing = (camera.x() - center.x) * pose.normal[0] * direction
                + (camera.z() - center.z) * pose.normal[2] * direction;
            if (facing <= 0) continue;
            std::vector<std::string_view> lines;
            font.wrap(data.text, ui::TextStyle::Pixel, pose.width, lines);
            std::string text;
            for (size_t line = 0; line < 4; ++line) {
                if (line) text += '\n';
                if (line < lines.size()) text += lines[line];
            }
            const uint8_t light = lightAt(center.x, center.y, center.z);
            const float brightness = data.glow ? 1.0f : float(std::max(light & 15, light >> 4)) / 15.0f;
            const mod::Color color {
                uint8_t(float((data.color >> 16) & 255) * brightness),
                uint8_t(float((data.color >> 8) & 255) * brightness),
                uint8_t(float(data.color & 255) * brightness), 255,
            };
            const std::array<float, 3> right { pose.right[0] * direction, 0, pose.right[2] * direction };
            painter.textOnPlane(center, text, color, right, { 0, 1, 0 }, pose.pixel, pose.width);
        }
    }
}

}
