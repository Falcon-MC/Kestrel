#include "ui/FallbackArt.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

namespace kestrel::ui {
namespace {

Color rgb(uint32_t value)
{
    return {static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value), 255};
}

class Canvas {
public:
    explicit Canvas(uint32_t width, uint32_t height)
    {
        image.width = width;
        image.height = height;
        image.rgba.resize(static_cast<size_t>(width) * height * 4);
    }

    void rect(int x, int y, int w, int h, Color color)
    {
        for (int row = std::max(0, y); row < std::min(int(image.height), y + h); ++row) {
            for (int col = std::max(0, x); col < std::min(int(image.width), x + w); ++col) {
                auto* pixel = image.rgba.data() + (static_cast<size_t>(row) * image.width + col) * 4;
                pixel[0] = color.r;
                pixel[1] = color.g;
                pixel[2] = color.b;
                pixel[3] = color.a;
            }
        }
    }

    void line(int x0, int y0, int x1, int y1, Color color, int thickness = 1)
    {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        for (;;) {
            rect(x0, y0, thickness, thickness, color);
            if (x0 == x1 && y0 == y1) break;
            int twice = error * 2;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    }

    void outline(int x, int y, int w, int h, Color color, int thickness = 1)
    {
        rect(x, y, w, thickness, color);
        rect(x, y + h - thickness, w, thickness, color);
        rect(x, y, thickness, h, color);
        rect(x + w - thickness, y, thickness, h, color);
    }

    void circle(int cx, int cy, int radius, Color color, bool filled = false)
    {
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                float d = std::hypot(float(x - cx), float(y - cy));
                if (d <= radius + 0.25f && (filled || d >= radius - 0.8f)) rect(x, y, 1, 1, color);
            }
        }
    }

    void path(std::initializer_list<std::pair<int, int>> points, Color color, int thickness = 1)
    {
        if (points.size() < 2) return;
        auto previous = points.begin();
        for (auto point = previous + 1; point != points.end(); ++point) {
            line(previous->first, previous->second, point->first, point->second, color, thickness);
            previous = point;
        }
    }

    void triangle(std::pair<float, float> a, std::pair<float, float> b, std::pair<float, float> c, Color color)
    {
        auto edge = [](auto p, auto q, float x, float y) { return (x - p.first) * (q.second - p.second) - (y - p.second) * (q.first - p.first); };
        float area = edge(a, b, c.first, c.second);
        if (std::abs(area) < 0.01f) return;
        int left = std::max(0, int(std::floor(std::min({a.first, b.first, c.first}))));
        int right = std::min(int(image.width), int(std::ceil(std::max({a.first, b.first, c.first}))));
        int top = std::max(0, int(std::floor(std::min({a.second, b.second, c.second}))));
        int bottom = std::min(int(image.height), int(std::ceil(std::max({a.second, b.second, c.second}))));
        for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
            float u = edge(b, c, x + 0.5f, y + 0.5f) / area;
            float v = edge(c, a, x + 0.5f, y + 0.5f) / area;
            float w = 1 - u - v;
            if (u < 0 || v < 0 || w < 0) continue;
            uint32_t hash = (uint32_t(x / 2) * 1664525u) ^ (uint32_t(y / 2) * 1013904223u);
            int grain = int((hash >> 19) % 17) - 8;
            Color textured = color;
            textured.r = static_cast<uint8_t>(std::clamp(int(color.r) + grain, 0, 255));
            textured.g = static_cast<uint8_t>(std::clamp(int(color.g) + grain, 0, 255));
            textured.b = static_cast<uint8_t>(std::clamp(int(color.b) + grain, 0, 255));
            rect(x, y, 1, 1, textured);
        }
    }

    Bitmap finish() { return std::move(image); }

private:
    Bitmap image;
};

struct Bevel {
    uint32_t border, face, highlight, shade, corner, depth;
};

Bevel palette(std::string_view name)
{
    bool down = name.find("pressed") != std::string_view::npos;
    bool hover = name.find("hovered") != std::string_view::npos;
    bool focus = name.find("focused") != std::string_view::npos;
    if (name.find("primary") != std::string_view::npos) {
        if (down) return {0x1e1e1f, 0x1d4d13, 0x4a7142, 0x345f2b, 0x5d7f55, 0x1d4d13};
        if (hover) return {0x1e1e1f, 0x2a641c, 0x7fa277, 0x6a9260, 0xa6bea0, 0x1d4d13};
        return {0x1e1e1f, 0x3c8527, 0x639d52, 0x50913d, 0x73a764, 0x1d4d13};
    }
    if (name.find("secondary") != std::string_view::npos) {
        if (down) return {0x1e1e1f, 0xb1b2b5, 0xc1c1c4, 0xb9babc, 0xc7c8c9, 0x58585a};
        if (hover) return {0x000000, 0xb1b2b5, 0xe0e0e1, 0xe0e0e1, 0xf3f3f3, 0x58585a};
        if (focus) return {0x1e1e1f, 0xd0d1d4, 0xd9dadd, 0xd5d6d8, 0xdddee0, 0x58585a};
        return {0x000000, 0xd0d1d4, 0xe3e3e5, 0xe3e3e5, 0xeeeeef, 0x58585a};
    }
    if (name.find("destructive") != std::string_view::npos) {
        if (down) return {0x1e1e1f, 0xad1d1d, 0xbd4a4a, 0xb53434, 0xc45d5d, 0xad1d1d};
        if (hover) return {0x1e1e1f, 0xc02d2d, 0xd98181, 0xd36c6c, 0xe5a7a7, 0xad1d1d};
        return {0x1e1e1f, 0xca3636, 0xd55e5e, 0xcf4a4a, 0xd96e6e, 0xad1d1d};
    }
    if (name.starts_with("realms_")) {
        if (down) return {0x1e1e1f, 0x4a1cac, 0x6e49bd, 0x5c33b4, 0x7d5cc3, 0x4a1cac};
        if (hover) return {0x1e1e1f, 0x5d2cc6, 0x9e80dd, 0x8e6bd7, 0xbba6e7, 0x4a1cac};
        return {0x1e1e1f, 0x7345e5, 0x8f6aea, 0x8158e8, 0x9a79ed, 0x4a1cac};
    }
    if (down) return {0x1e1e1f, 0x313233, 0x5a5b5c, 0x464747, 0x6b6c6c, 0x313233};
    if (hover) return {0x1e1e1f, 0x58585a, 0x79797b, 0x69696b, 0x878789, 0x313233};
    return {0x1e1e1f, 0x48494a, 0x6d6d6e, 0x5a5b5c, 0x7b7c7d, 0x313233};
}

Bitmap button(std::string_view name)
{
    bool disabled = name.find("disabled") != std::string_view::npos;
    if (disabled) {
        Canvas canvas(3, 5);
        canvas.rect(0, 0, 3, 5, theme::Disabled);
        canvas.rect(1, 1, 1, 1, theme::Muted1);
        canvas.rect(1, 2, 1, 2, rgb(0x8c8d90));
        return canvas.finish();
    }
    bool focused = name.find("focused") != std::string_view::npos;
    bool pressed = name.find("pressed") != std::string_view::npos;
    int inset = focused ? 1 : 0;
    int height = pressed ? 5 : 7;
    Canvas canvas(5 + inset * 2, height + inset * 2);
    if (focused) canvas.outline(0, 0, 7, height + 2, theme::White);
    auto colors = palette(name);
    canvas.rect(inset, inset, 5, height, rgb(colors.border));
    canvas.rect(inset + 1, inset + 1, 3, 3, rgb(colors.face));
    canvas.rect(inset + 1, inset + 1, 2, 1, rgb(colors.highlight));
    canvas.rect(inset + 1, inset + 2, 1, 1, rgb(colors.highlight));
    canvas.rect(inset + 2, inset + 3, 2, 1, rgb(colors.shade));
    canvas.rect(inset + 3, inset + 2, 1, 1, rgb(colors.shade));
    canvas.rect(inset + 3, inset + 1, 1, 1, rgb(colors.corner));
    canvas.rect(inset + 1, inset + 3, 1, 1, rgb(colors.corner));
    if (!pressed) canvas.rect(inset + 1, inset + 4, 3, 2, rgb(colors.depth));
    return canvas.finish();
}

Bitmap textField(std::string_view name)
{
    bool disabled = name.ends_with("disabled");
    Canvas canvas(6, 7);
    canvas.rect(0, 0, 6, 7, disabled ? theme::Disabled : theme::Divider);
    canvas.rect(1, 1, 4, 5, disabled ? theme::Muted1 : theme::PanelDark);
    canvas.rect(1, 1, 4, disabled ? 1 : 2, disabled ? rgb(0x8c8d90) : rgb(0x242425));
    if (name.ends_with("neutral90")) {
        canvas.rect(1, 1, 4, 2, theme::Divider);
        canvas.rect(1, 3, 4, 3, rgb(0x242425));
    }
    return canvas.finish();
}

void portrait(Canvas& canvas, int x, int y, bool alex)
{
    auto hair = rgb(alex ? 0xcb8537 : 0x513a28);
    canvas.rect(x, y, 10, 10, theme::Divider);
    canvas.rect(x + 1, y + 1, 8, 8, rgb(alex ? 0xf0c794 : 0xb68a65));
    canvas.rect(x + 1, y + 1, 8, 3, hair);
    canvas.rect(x + 1, y + 4, 1, 2, hair);
    canvas.rect(x + 2, y + 5, 2, 1, theme::White);
    canvas.rect(x + 6, y + 5, 2, 1, theme::White);
    canvas.rect(x + 3, y + 5, 1, 1, rgb(alex ? 0x3b8738 : 0x6650a4));
    canvas.rect(x + 6, y + 5, 1, 1, rgb(alex ? 0x3b8738 : 0x6650a4));
    canvas.rect(x + 4, y + 7, 2, 1, rgb(0x8c573c));
}

// An original scene built from colored cuboids, without importing a screenshot or block texture.
Bitmap landscape()
{
    Canvas canvas(320, 180);
    canvas.rect(0, 0, 320, 180, rgb(0x86b9e0));
    canvas.rect(0, 65, 320, 50, rgb(0xa2cae7));
    struct Block {
        float x, y, z, width, height, depth;
        Color top, side;
    };
    std::vector<Block> blocks;
    for (int z = 0; z < 25; ++z) {
        for (int x = -16; x < 16; ++x) {
            float height = 1 + std::floor((std::sin(x * 0.22f) + std::cos(z * 0.25f)) * 0.8f);
            bool water = std::abs(x) < 6 && z > 7 && z < 22;
            bool shore = std::abs(x) < 8 && z > 5 && z < 24;
            if (water) height = 0.25f;
            Color top = water ? rgb(0x4b87ba) : shore ? rgb(0xc5c3ae) : rgb(0x779453);
            Color side = shore ? rgb(0x929484) : rgb(0x7b6652);
            blocks.push_back({float(x), -3, float(z), 1, height + 3, 1, top, side});
        }
    }
    for (auto tree : {std::pair {-5, 2}, std::pair {7, 4}, std::pair {-8, 10}, std::pair {9, 15}}) {
        float x = float(tree.first), z = float(tree.second);
        for (int y = 1; y < 10; ++y) {
            blocks.push_back({x, float(y), z, 0.7f, 1, 0.7f, rgb(0x67515a), rgb(0x49333f)});
        }
        for (int branch = -3; branch <= 3; ++branch) {
            blocks.push_back({x + branch, 7 + std::abs(branch) * 0.25f, z, 1.2f, 0.6f, 0.7f, rgb(0x67515a), rgb(0x49333f)});
        }
        for (int dz = -3; dz <= 3; ++dz) {
            for (int dx = -4; dx <= 4; ++dx) {
                if (dx * dx + dz * dz > 22) continue;
                float y = 8 + ((dx * dx + dz * dz) < 9 ? 1.5f : 0.0f);
                bool pale = (dx + dz) % 3 == 0;
                blocks.push_back({x + dx, y, z + dz, 1.4f, 1.3f, 1.4f,
                    pale ? rgb(0xe9bbcc) : rgb(0xd6a0b9), pale ? rgb(0xc997b0) : rgb(0xac789b)});
            }
        }
    }
    std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
        if (a.z != b.z) return a.z > b.z;
        return a.y < b.y;
    });
    auto project = [](float x, float y, float z) {
        float distance = z + 11;
        return std::pair {160 + x * 140 / distance, 75 - (y - 6 + distance * 0.08f) * 140 / distance};
    };
    auto shade = [](Color color, float fraction) {
        color.r = static_cast<uint8_t>(color.r * fraction);
        color.g = static_cast<uint8_t>(color.g * fraction);
        color.b = static_cast<uint8_t>(color.b * fraction);
        return color;
    };
    auto face = [&](auto a, auto b, auto c, auto d, Color color) {
        canvas.triangle(a, b, c, color);
        canvas.triangle(a, c, d, color);
    };
    for (const auto& b : blocks) {
        float right = b.x + b.width, top = b.y + b.height, back = b.z + b.depth;
        auto a = project(b.x, top, b.z), c = project(right, top, back);
        face(a, project(right, top, b.z), project(right, b.y, b.z), project(b.x, b.y, b.z), shade(b.side, 0.85f));
        if (b.x > 0) face(a, project(b.x, top, back), project(b.x, b.y, back), project(b.x, b.y, b.z), b.side);
        else face(project(right, top, b.z), c, project(right, b.y, back), project(right, b.y, b.z), b.side);
        face(a, project(right, top, b.z), c, project(b.x, top, back), b.top);
    }
    return canvas.finish();
}

bool icon(std::string_view name, Bitmap& out)
{
    const Color ink = theme::Black;
    const Color white = theme::White;
    bool framed = name == "accessibility" || name == "account" || name == "keyboard-mouse" || name == "touch"
        || name == "general-icon" || name == "language" || name == "resource-packs-icon" || name == "server"
        || name == "sound-block" || name == "storage";
    Canvas canvas(framed ? 24 : 16, framed ? 24 : 16);
    if (framed) {
        canvas.rect(0, 0, 24, 24, theme::Divider);
        canvas.rect(2, 2, 20, 20, rgb(0x8f8b80));
    }
    if (name == "Close") {
        canvas = Canvas(10, 10);
        canvas.line(0, 0, 9, 9, white, 2);
        canvas.line(8, 0, 0, 8, white, 2);
    } else if (name == "Plus") {
        canvas = Canvas(24, 24);
        canvas.rect(11, 4, 2, 16, white);
        canvas.rect(4, 11, 16, 2, white);
    } else if (name == "arrowBack") {
        canvas = Canvas(8, 16);
        canvas.path({{6, 1}, {0, 7}, {6, 13}}, ink, 2);
    } else if (name == "arrowUp" || name == "arrowDown") {
        canvas = Canvas(12, 6);
        bool up = name == "arrowUp";
        canvas.path({{1, up ? 4 : 0}, {5, up ? 0 : 4}, {9, up ? 4 : 0}}, white, 2);
    } else if (name == "ArrowLeft" || name == "ArrowRight") {
        canvas = Canvas(36, 36);
        canvas.rect(4, 0, 28, 36, rgb(0x5a5a5a));
        canvas.rect(0, 4, 36, 28, rgb(0x5a5a5a));
        canvas.rect(4, 4, 28, 28, rgb(0xd0d1d4));
        canvas.rect(6, 2, 24, 4, rgb(0xe6e8eb));
        canvas.rect(2, 6, 4, 24, rgb(0xe6e8eb));
        canvas.rect(6, 30, 24, 4, rgb(0x9b9b9b));
        canvas.rect(30, 6, 4, 24, rgb(0x9b9b9b));
        bool left = name == "ArrowLeft";
        for (int row = -6; row <= 6; ++row) {
            int width = 7 - std::abs(row);
            canvas.rect(left ? 20 - width : 15, 18 + row, width, 1, ink);
        }
    } else if (name == "ExternalLink") {
        canvas = Canvas(25, 24);
        canvas.path({{9, 2}, {2, 2}, {2, 22}, {22, 22}, {22, 15}}, ink, 2);
        canvas.path({{15, 0}, {23, 0}, {23, 8}}, ink, 2);
        canvas.line(11, 12, 22, 1, ink, 2);
    } else if (name == "search") {
        canvas.circle(6, 6, 5, white);
        canvas.line(10, 10, 15, 15, white, 2);
    } else if (name == "options@0.5x.icon") {
        canvas = Canvas(2, 8);
        for (int y = 0; y < 8; y += 3) canvas.rect(0, y, 2, 2, ink);
    } else if (name == "gamerscore_small@0.5x.icon") {
        canvas = Canvas(7, 7);
        canvas.circle(3, 3, 3, ink);
        canvas.path({{5, 2}, {2, 2}, {2, 4}, {4, 4}, {4, 3}}, ink);
    } else if (name == "time@0.5x.icon") {
        canvas = Canvas(12, 12);
        canvas.circle(5, 5, 5, ink);
        canvas.path({{5, 2}, {5, 5}, {7, 7}}, ink);
    } else if (name == "pickaxe@0.5x.icon") {
        canvas = Canvas(12, 12);
        canvas.line(1, 10, 8, 3, ink, 2);
        canvas.path({{3, 1}, {8, 1}, {10, 3}, {10, 7}}, ink);
        canvas.path({{3, 2}, {7, 2}, {9, 4}, {9, 7}}, ink);
    } else if (name == "sword@0.5x.icon") {
        canvas = Canvas(12, 12);
        canvas.path({{4, 7}, {9, 1}, {11, 1}, {11, 3}, {6, 9}}, ink);
        canvas.line(1, 10, 4, 7, ink, 2);
        canvas.line(1, 5, 7, 11, ink);
    } else if (name == "boots@0.5x.icon") {
        canvas = Canvas(11, 10);
        canvas.path({{1, 0}, {4, 0}, {4, 6}, {6, 8}, {6, 9}, {1, 9}, {1, 0}}, ink);
        canvas.path({{6, 0}, {8, 0}, {8, 5}, {10, 7}, {10, 9}, {7, 9}}, ink);
    } else if (name == "hanger@0.5x.icon") {
        canvas = Canvas(12, 12);
        canvas.path({{4, 2}, {4, 1}, {7, 1}, {7, 3}, {5, 5}, {5, 6}}, ink);
        canvas.path({{5, 6}, {0, 9}, {0, 11}, {11, 11}, {11, 9}, {5, 6}}, ink);
    } else if (name == "image@0.5x.icon" || name == "edit-image@0.5x.icon") {
        canvas = Canvas(12, 12);
        canvas.outline(0, 2, 10, 10, ink);
        canvas.rect(2, 4, 2, 2, ink);
        canvas.path({{1, 10}, {4, 7}, {6, 9}, {8, 6}, {9, 10}}, ink);
        if (name == "edit-image@0.5x.icon") {
            canvas.line(5, 6, 10, 1, white, 3);
            canvas.line(5, 6, 10, 1, ink, 2);
        }
    } else if (name == "achievements@0.5x.icon") {
        canvas = Canvas(12, 12);
        canvas.outline(0, 1, 12, 11, ink);
        canvas.outline(2, 0, 3, 8, ink);
        canvas.outline(7, 0, 3, 8, ink);
        canvas.path({{2, 8}, {3, 10}, {4, 8}}, ink);
        canvas.path({{7, 8}, {8, 10}, {9, 8}}, ink);
    } else if (name == "friends" || name == "party") {
        if (name == "party") canvas = Canvas(24, 24);
        portrait(canvas, name == "party" ? 10 : 6, 0, false);
        portrait(canvas, name == "party" ? 1 : 0, 6, true);
        if (name == "party") portrait(canvas, 12, 13, true);
    } else if (name == "Realms") {
        canvas = Canvas(24, 24);
        canvas.rect(0, 0, 24, 24, rgb(0x201c25));
        canvas.rect(2, 2, 20, 20, rgb(0x15121b));
        canvas.rect(4, 4, 16, 16, rgb(0x36234e));
        canvas.rect(6, 6, 12, 12, rgb(0x832ed4));
        canvas.rect(6, 6, 4, 8, rgb(0x9746e1));
        canvas.rect(10, 6, 4, 2, rgb(0xad63ec));
        canvas.rect(14, 8, 4, 6, rgb(0x9746e1));
        canvas.rect(6, 14, 4, 4, rgb(0xad63ec));
        canvas.rect(10, 16, 8, 2, rgb(0x9746e1));
    } else if (name == "World") {
        canvas = Canvas(24, 24);
        canvas.circle(11, 11, 10, white, true);
        canvas.path({{9, 2}, {15, 2}, {16, 5}, {12, 9}, {12, 11}, {8, 14}, {8, 18}, {14, 20}}, rgb(0x646464), 4);
    } else if (name == "Settings") {
        canvas = Canvas(24, 24);
        for (int row = 0; row < 3; ++row) {
            canvas.rect(4, 5 + row * 7, 16, 2, white);
            canvas.rect(row == 1 ? 15 : 8, 3 + row * 7, 2, 6, white);
        }
    } else if (name == "cursor_gamepad_brackets") {
        canvas = Canvas(24, 24);
        canvas.path({{5, 4}, {2, 4}, {2, 20}, {5, 20}}, white);
        canvas.path({{18, 4}, {21, 4}, {21, 20}, {18, 20}}, white);
        canvas.rect(6, 8, 12, 8, rgb(0xaaaaaa));
        canvas.rect(7, 11, 5, 2, ink);
        canvas.rect(9, 9, 2, 5, ink);
        canvas.rect(15, 10, 2, 2, ink);
        canvas.rect(13, 13, 2, 2, ink);
    } else if (name == "accessibility") {
        canvas.rect(2, 2, 20, 20, rgb(0x306bde));
        canvas.rect(9, 4, 6, 6, white);
        canvas.rect(4, 12, 16, 2, white);
        canvas.rect(8, 14, 3, 6, white);
        canvas.rect(13, 14, 3, 6, white);
    } else if (name == "account") {
        canvas.rect(2, 2, 20, 20, rgb(0x444444));
        canvas.outline(7, 4, 10, 9, white, 2);
        canvas.path({{5, 20}, {5, 16}, {9, 12}, {14, 12}, {18, 16}, {18, 20}}, white, 2);
    } else if (name == "keyboard-mouse") {
        canvas.rect(7, 6, 10, 15, rgb(0x55534b));
        canvas.rect(9, 4, 6, 2, rgb(0x55534b));
        canvas.rect(10, 6, 4, 6, rgb(0xb1afa1));
        canvas.rect(9, 19, 6, 2, rgb(0xcac6b5));
    } else if (name == "touch") {
        canvas.path({{5, 17}, {5, 12}, {7, 10}, {10, 10}, {10, 4}, {13, 4}, {13, 10}, {16, 10}, {19, 13}, {19, 19}, {16, 21}, {9, 21}, {5, 17}}, rgb(0x49473d), 2);
    } else if (name == "resource-packs-icon") {
        canvas.rect(2, 2, 20, 20, rgb(0x675038));
        canvas.rect(4, 4, 16, 16, rgb(0xa66e27));
        canvas.rect(4, 10, 16, 2, rgb(0x675038));
        canvas.rect(10, 8, 4, 8, rgb(0xaaa69a));
    } else if (name == "server") {
        canvas.rect(2, 2, 20, 20, rgb(0x393955));
        canvas.circle(11, 11, 10, rgb(0x9dceeb), true);
        canvas.rect(4, 4, 6, 3, rgb(0xa4d87c));
        canvas.rect(3, 8, 7, 3, rgb(0x83c657));
        canvas.rect(6, 11, 4, 8, rgb(0x80bd51));
        canvas.rect(15, 5, 6, 4, rgb(0x75b944));
        canvas.rect(13, 11, 7, 4, rgb(0x8fcb5f));
        canvas.rect(15, 15, 3, 5, rgb(0x75b944));
    } else if (name == "sound-block") {
        canvas.rect(2, 2, 20, 20, rgb(0xad582e));
        for (int y = 4; y < 20; y += 4) {
            for (int x = 4; x < 20; x += 4) canvas.outline(x, y, 3, 3, rgb(0x66311b));
        }
    } else if (name == "storage") {
        canvas.rect(2, 2, 20, 20, rgb(0x66552d));
        for (int y = 3; y < 21; y += 4) {
            canvas.rect(3, y, 18, 2, rgb(0xb4995d));
            canvas.rect(5, y + 2, 14, 2, rgb(0x988049));
        }
    } else if (name == "general-icon") {
        canvas.rect(2, 2, 20, 20, rgb(0x413623));
        canvas.circle(11, 11, 8, rgb(0xa96530), true);
        for (int i = 0; i < 4; ++i) {
            canvas.rect(4 + i * 4, 4, 2, 3, rgb(0xc49950));
            canvas.rect(4 + i * 4, 17, 2, 3, rgb(0xc49950));
        }
        canvas.rect(9, 9, 5, 5, rgb(0x654221));
    } else if (name == "language") {
        canvas.rect(2, 2, 20, 20, rgb(0x2c6590));
        for (int y = 2; y < 22; ++y) canvas.rect(24 - y, y, y - 2, 1, rgb(0x269cab));
        canvas.path({{4, 7}, {10, 7}, {10, 10}, {5, 10}, {9, 14}, {11, 12}}, white, 2);
        canvas.path({{14, 19}, {17, 12}, {20, 19}}, white, 2);
        canvas.rect(15, 17, 5, 1, white);
    } else {
        return false;
    }
    out = canvas.finish();
    return true;
}
}

bool drawFallbackArt(std::string_view name, Bitmap& bitmap)
{
    if (!name.starts_with("hbui/")) return false;
    name.remove_prefix(5);
    if (name == "pressable_elevated_disabled") {
        bitmap = button(name);
        return true;
    }
    for (auto family : {"pressable_elevated_primary_", "pressable_primary_", "pressable_elevated_secondary_",
             "pressable_secondary_", "pressable_elevated_destructive_", "pressable_destructive_",
             "tabBar_neutral_", "realms_pressable_elevated_", "realms_pressable_"}) {
        std::string_view prefix(family);
        if (!name.starts_with(prefix)) continue;
        auto state = name.substr(prefix.size());
        if (state != "default" && state != "hovered" && state != "focused" && state != "pressed"
            && state != "default_focused" && state != "pressed_focused") return false;
        bitmap = button(name);
        return true;
    }
    if (name == "baseTextField_neutral80" || name == "baseTextField_neutral90" || name == "baseTextField_disabled") {
        bitmap = textField(name);
        return true;
    }
    if (name == "screenshot_3") {
        bitmap = landscape();
        return true;
    }
    return icon(name, bitmap);
}

BorderImage fallbackBorder(std::string_view component, std::string_view state)
{
    BorderImage result;
    bool focused = state == "Focused" || state == "PressedFocused";
    bool pressed = state == "Pressed" || state == "PressedFocused";
    bool disabled = state == "Disabled";
    std::string suffix = disabled ? "disabled" : pressed ? "pressed" : state == "Hovered" ? "hovered" : "default";
    if (focused) suffix += "_focused";
    if (component == "baseTextField") {
        result.sprite = disabled ? "hbui/baseTextField_disabled" : "hbui/baseTextField_neutral80";
        result.slice = {1, 3, 1, 1};
        result.width = {0.2f * theme::Rem, 0.6f * theme::Rem, 0.2f * theme::Rem, 0.2f * theme::Rem};
    } else {
        if (component == "tabBarNeutral") result.sprite = "hbui/tabBar_neutral_" + suffix;
        else if (component == "pressableElevatedPrimary") result.sprite = "hbui/pressable_" + std::string(pressed ? "primary_" : "elevated_primary_") + suffix;
        else if (component == "pressableElevatedSecondary") result.sprite = "hbui/pressable_" + std::string(pressed ? "secondary_" : "elevated_secondary_") + suffix;
        else if (component == "pressableElevatedDestructive") result.sprite = "hbui/pressable_" + std::string(pressed ? "destructive_" : "elevated_destructive_") + suffix;
        else return result;
        if (disabled) {
            result.sprite = "hbui/pressable_elevated_disabled";
            result.slice = {1, 1, 1, 3};
        } else {
            float edge = focused ? 3.0f : 2.0f;
            result.slice = {edge, edge, edge, edge + (pressed ? 0.0f : 2.0f)};
            if (focused) result.outset = {0.2f * theme::Rem, 0.2f * theme::Rem, 0.2f * theme::Rem, 0.2f * theme::Rem};
        }
        result.width = {result.slice.left * theme::Rem / 5, result.slice.top * theme::Rem / 5,
            result.slice.right * theme::Rem / 5, result.slice.bottom * theme::Rem / 5};
    }
    result.fill = true;
    result.valid = true;
    return result;
}
}
