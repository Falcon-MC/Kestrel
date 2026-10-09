#include "client/ActorExtent.h"
#include "client/Client.h"
#include "client/AttachableFrame.h"
#include "client/motion/MotionMath.h"
#include "world/CrystalBeam.h"
#include "world/ItemGlint.h"

#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/Image.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_set>

namespace kestrel {

namespace {

constexpr double ActorCandidateRadius = 72.0;
constexpr double MaxPlayerDistance = 192.0;
constexpr float PlayerWidth = 0.6f;
constexpr float PlayerHeight = 1.8f;
constexpr float DefaultActorWidth = 1.0f;
constexpr float DefaultActorHeight = 2.0f;
constexpr float PoseMargin = 0.5f;
constexpr uint32_t EntityQuadFlag = 1u << 5;
constexpr uint32_t AdditiveQuadFlag = 1u << 6;
// Bit 9 maps to the shader's animated UV flag.
constexpr uint32_t FoggedQuadFlag = 1u << 10;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
constexpr int SwimmingFlag = 57;
constexpr int GlidingFlag = 32;
constexpr double HeadClearance = 0.7;
constexpr double ExtraLineRaise = 0.125;
constexpr double StandingHeight = 1.8;
constexpr double SneakingHeight = 1.5;
constexpr double ScoreTagDistance = 10.0;
constexpr float CrosshairRadius = 48.0f;
constexpr float NameTagPixelSize = 1.6f / 60.0f;
constexpr uint64_t SneakingFlag = 1ull << 1;
constexpr uint64_t UsingItemFlag = 1ull << 4;
constexpr double TicksPerSecond = 20.0;
constexpr uint64_t InvisibleFlag = 1ull << 5;
constexpr uint64_t CanShowNameFlag = 1ull << 14;
constexpr uint64_t AlwaysShowNameFlag = 1ull << 15;



/**
 * A float as a 16-bit half float, rounded to nearest, clamped to the largest
 * finite half and flushed to zero below the smallest normal one.
 */
uint16_t toHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7bffu);
    }
    uint32_t half = (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
    if (mantissa & 0x1000u) {
        ++half;
    }
    return static_cast<uint16_t>(sign | std::min(half, 0x7bffu));
}

constexpr const char* PersonaBlinkAlias = "blink";
constexpr double SkinFramesPerSecond = 7.0;
constexpr float BlinkFrameOffset = 0.5f;

/**
 * The engine variable a persona animation controller reads the frame count
 * of a sheet of that kind from.
 */
const char* skinFramesVariable(SkinAnimationKind kind)
{
    switch (kind) {
    case SkinAnimationKind::Face:
        return "animation_frames_face";
    case SkinAnimationKind::Body32:
        return "animation_frames_32x32";
    default:
        return "animation_frames_128x128";
    }
}

const world::molang::Script& lifeTimeScript()
{
    static const world::molang::Script script = world::molang::Script::compile("query.life_time");
    return script;
}

const world::molang::Script& blinkingScript()
{
    static const world::molang::Script script = world::molang::Script::compile("variable.is_blinking");
    return script;
}

/**
 * How far down its sheet an animation is: a blinking face shows its second
 * half while the blink controller says it blinks, every other sheet steps
 * through its frames seven times a second over the entity's life.
 */
float skinFrameOffset(SkinAnimationKind kind, bool blinking, uint32_t frames, double lifeTime, double blinkState)
{
    if (kind == SkinAnimationKind::Face && blinking) {
        return static_cast<float>(blinkState) * BlinkFrameOffset;
    }
    double count = static_cast<double>(std::max<uint32_t>(frames, 1));
    double frame = std::fmod(std::floor(lifeTime * SkinFramesPerSecond), count);
    if (frame < 0.0) {
        frame += count;
    }
    return static_cast<float>(frame / count);
}

struct QuadCorner {
    std::array<float, 3> position {};
    std::array<float, 2> uv {};
};

world::ModelQuadGpu packCorners(const std::array<QuadCorner, 4>& corners, uint32_t layer, uint32_t shadeWord)
{
    world::ModelQuadGpu gpu;
    std::array<std::array<float, 3>, 4> positions;
    for (size_t corner = 0; corner < 4; ++corner) positions[corner] = corners[corner].position;
    packEntityPositions(positions, gpu.words);
    for (size_t corner = 0; corner < 4; ++corner) {
        uint32_t u = static_cast<uint32_t>(std::clamp(corners[corner].uv[0] * 4096.0f + 0.5f, 0.0f, 65535.0f));
        uint32_t v = static_cast<uint32_t>(std::clamp(corners[corner].uv[1] * 4096.0f + 0.5f, 0.0f, 65535.0f));
        gpu.words[6 + corner] = u | (v << 16);
    }
    gpu.words[10] = layer;
    gpu.words[11] = shadeWord;
    gpu.words[12] = FullSkyLight;
    return gpu;
}

/**
 * Keeps the part of a polygon on one side of a UV line, cutting the edges
 * that cross it with positions following the UVs along each edge.
 */
std::vector<QuadCorner> clipUv(const std::vector<QuadCorner>& polygon, size_t axis, float bound, bool above)
{
    std::vector<QuadCorner> kept;
    for (size_t index = 0; index < polygon.size(); ++index) {
        const QuadCorner& from = polygon[index];
        const QuadCorner& to = polygon[(index + 1) % polygon.size()];
        bool fromInside = above ? from.uv[axis] >= bound : from.uv[axis] <= bound;
        bool toInside = above ? to.uv[axis] >= bound : to.uv[axis] <= bound;
        if (fromInside) {
            kept.push_back(from);
        }
        if (fromInside == toInside) {
            continue;
        }
        float t = (bound - from.uv[axis]) / (to.uv[axis] - from.uv[axis]);
        QuadCorner cut;
        for (size_t component = 0; component < 3; ++component) {
            cut.position[component] = from.position[component] + (to.position[component] - from.position[component]) * t;
        }
        cut.uv[0] = from.uv[0] + (to.uv[0] - from.uv[0]) * t;
        cut.uv[1] = from.uv[1] + (to.uv[1] - from.uv[1]) * t;
        cut.uv[axis] = bound;
        kept.push_back(cut);
    }
    return kept;
}

/**
 * Pushes a triangle whose texture is spread over a grid of layers: it is
 * clipped to each tile its UVs reach, and every piece is fanned into
 * triangles drawn as quads repeating their last corner.
 */
void appendTiledTriangle(const std::array<QuadCorner, 4>& corners, uint32_t layer, const world::EntityTileGrid& grid, uint32_t shadeWord, std::vector<world::ModelQuadGpu>& out)
{
    std::vector<QuadCorner> triangle { corners[0], corners[1], corners[2] };
    std::array<float, 2> low { 1.0f, 1.0f };
    std::array<float, 2> high { 0.0f, 0.0f };
    for (QuadCorner& corner : triangle) {
        corner.uv[0] *= grid.coverX;
        corner.uv[1] *= grid.coverY;
        for (size_t axis = 0; axis < 2; ++axis) {
            low[axis] = std::min(low[axis], corner.uv[axis]);
            high[axis] = std::max(high[axis], corner.uv[axis]);
        }
    }
    std::array<uint32_t, 2> tiles { grid.tilesX, grid.tilesY };
    std::array<uint32_t, 2> first {};
    std::array<uint32_t, 2> last {};
    for (size_t axis = 0; axis < 2; ++axis) {
        first[axis] = std::min(static_cast<uint32_t>(std::max(low[axis], 0.0f) * float(tiles[axis])), tiles[axis] - 1);
        last[axis] = std::min(static_cast<uint32_t>(std::max(high[axis], 0.0f) * float(tiles[axis])), tiles[axis] - 1);
    }
    for (uint32_t tileY = first[1]; tileY <= last[1]; ++tileY) {
        for (uint32_t tileX = first[0]; tileX <= last[0]; ++tileX) {
            std::vector<QuadCorner> piece = triangle;
            if (tileX > 0) {
                piece = clipUv(piece, 0, float(tileX) / float(tiles[0]), true);
            }
            if (tileX + 1 < tiles[0]) {
                piece = clipUv(piece, 0, float(tileX + 1) / float(tiles[0]), false);
            }
            if (tileY > 0) {
                piece = clipUv(piece, 1, float(tileY) / float(tiles[1]), true);
            }
            if (tileY + 1 < tiles[1]) {
                piece = clipUv(piece, 1, float(tileY + 1) / float(tiles[1]), false);
            }
            if (piece.size() < 3) {
                continue;
            }
            for (QuadCorner& corner : piece) {
                corner.uv[0] = std::clamp(corner.uv[0] * float(tiles[0]) - float(tileX), 0.0f, 1.0f);
                corner.uv[1] = std::clamp(corner.uv[1] * float(tiles[1]) - float(tileY), 0.0f, 1.0f);
            }
            for (size_t fan = 1; fan + 1 < piece.size(); ++fan) {
                std::array<QuadCorner, 4> part { piece[0], piece[fan], piece[fan + 1], piece[fan + 1] };
                out.push_back(packCorners(part, layer + tileY * tiles[0] + tileX, shadeWord));
            }
        }
    }
}

/**
 * Pushes a quad whose texture may be spread over a grid of layers. Such a
 * quad is cut along the tile edges it crosses, each piece sampling the one
 * layer under it with its UVs moved into that tile; a quad repeating its last
 * corner is a triangle and is clipped to the tiles instead.
 */
void appendTiled(std::array<QuadCorner, 4> corners, uint32_t layer, const world::EntityTileGrid& grid, uint32_t shadeWord, std::vector<world::ModelQuadGpu>& out)
{
    if (grid.single()) {
        out.push_back(packCorners(corners, layer, shadeWord));
        return;
    }
    if (corners[2].position == corners[3].position && corners[2].uv == corners[3].uv) {
        appendTiledTriangle(corners, layer, grid, shadeWord, out);
        return;
    }
    uint32_t tilesX = grid.tilesX;
    uint32_t tilesY = grid.tilesY;
    for (QuadCorner& corner : corners) {
        corner.uv[0] *= grid.coverX;
        corner.uv[1] *= grid.coverY;
    }
    auto cuts = [&](const QuadCorner& from, const QuadCorner& to) {
        std::vector<float> list { 0.0f, 1.0f };
        for (size_t axis = 0; axis < 2; ++axis) {
            float a = from.uv[axis];
            float b = to.uv[axis];
            float tiles = static_cast<float>(axis == 0 ? tilesX : tilesY);
            if (std::abs(b - a) < 1.0e-6f) {
                continue;
            }
            float high = std::max(a, b) * tiles;
            for (float edge = std::floor(std::min(a, b) * tiles) + 1.0f; edge < high; edge += 1.0f) {
                float t = (edge / tiles - a) / (b - a);
                if (t > 1.0e-4f && t < 1.0f - 1.0e-4f) {
                    list.push_back(t);
                }
            }
        }
        std::sort(list.begin(), list.end());
        return list;
    };
    std::vector<float> across = cuts(corners[0], corners[1]);
    std::vector<float> down = cuts(corners[0], corners[3]);
    auto at = [&](float s, float t) {
        QuadCorner point;
        float weights[4] = { (1.0f - s) * (1.0f - t), s * (1.0f - t), s * t, (1.0f - s) * t };
        for (size_t corner = 0; corner < 4; ++corner) {
            for (size_t axis = 0; axis < 3; ++axis) {
                point.position[axis] += corners[corner].position[axis] * weights[corner];
            }
            point.uv[0] += corners[corner].uv[0] * weights[corner];
            point.uv[1] += corners[corner].uv[1] * weights[corner];
        }
        return point;
    };
    for (size_t row = 0; row + 1 < down.size(); ++row) {
        for (size_t column = 0; column + 1 < across.size(); ++column) {
            std::array<QuadCorner, 4> piece {
                at(across[column], down[row]), at(across[column + 1], down[row]),
                at(across[column + 1], down[row + 1]), at(across[column], down[row + 1]),
            };
            float centerU = (piece[0].uv[0] + piece[2].uv[0]) * 0.5f;
            float centerV = (piece[0].uv[1] + piece[2].uv[1]) * 0.5f;
            uint32_t tileX = std::min(static_cast<uint32_t>(std::max(centerU, 0.0f) * float(tilesX)), tilesX - 1);
            uint32_t tileY = std::min(static_cast<uint32_t>(std::max(centerV, 0.0f) * float(tilesY)), tilesY - 1);
            for (QuadCorner& corner : piece) {
                corner.uv[0] = std::clamp(corner.uv[0] * float(tilesX) - float(tileX), 0.0f, 1.0f);
                corner.uv[1] = std::clamp(corner.uv[1] * float(tilesY) - float(tileY), 0.0f, 1.0f);
            }
            out.push_back(packCorners(piece, layer + tileY * tilesX + tileX, shadeWord));
        }
    }
}

/**
 * Clip space of a point relative to the camera, x and y in normalized device
 * coordinates with y up, camera depth and buffer depth, or nothing outside
 * the camera's near/far planes.
 */
std::optional<std::array<double, 4>> project(const Mat4& matrix, double x, double y, double z)
{
    auto row = [&](size_t r) {
        return double(matrix[r]) * x + double(matrix[4 + r]) * y + double(matrix[8 + r]) * z + double(matrix[12 + r]);
    };
    double w = row(3);
    if (w <= 0.05) {
        return std::nullopt;
    }
    double depth = row(2) / w;
    if (depth < 0.0 || depth > 1.0) return std::nullopt;
    return std::array<double, 4> { row(0) / w, row(1) / w, w, depth };
}

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

float wrapDegrees(float degrees)
{
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
}

/**
 * A player's body trailing its head the way the game turns it, advanced by
 * ticks while it moves stepX and stepZ blocks a tick: it swings toward the
 * walking direction (facing forward while backing up), never lets the head
 * turn more than 75 degrees away, and creeps after the head once it is past
 * 50. Servers only send where players look, so every client works this out.
 */
float trailBody(float body, float head, double stepX, double stepZ, float ticks)
{
    if (stepX * stepX + stepZ * stepZ > 0.0025) {
        float heading = static_cast<float>(std::atan2(-stepX, stepZ) * 180.0 / 3.14159265358979);
        if (std::abs(wrapDegrees(head - heading)) > 95.0f) {
            heading += 180.0f;
        }
        body += wrapDegrees(heading - body) * (1.0f - std::pow(0.7f, ticks));
    }
    float turn = std::clamp(wrapDegrees(head - body), -75.0f, 75.0f);
    body = head - turn;
    if (std::abs(turn) > 50.0f) {
        body += turn * (1.0f - std::pow(0.8f, ticks));
    }
    return wrapDegrees(body);
}

/**
 * Case insensitive match of a bone name against a lowercase pattern where '*'
 * stands for any run of characters.
 */
bool matchesPattern(const std::string& pattern, const std::string& name)
{
    size_t p = 0;
    size_t n = 0;
    size_t star = std::string::npos;
    size_t resume = 0;
    while (n < name.size()) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[n])));
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = n;
        } else if (p < pattern.size() && pattern[p] == c) {
            ++p;
            ++n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

/**
 * The choice an entity's selector expression lands on, clamped into the
 * choice list; NoEntityChoice when there is none.
 */
uint32_t pickChoice(world::EntityAnimator& animator, const world::molang::Script& selector, const std::vector<uint32_t>& choices)
{
    if (choices.empty()) {
        return world::NoEntityChoice;
    }
    double value = animator.evaluate(selector);
    size_t index = std::isfinite(value) && value > 0.0 ? std::min(static_cast<size_t>(value), choices.size() - 1) : 0;
    return choices[index];
}

world::BoneMatrix compose(const world::BoneMatrix& a, const world::BoneMatrix& b)
{
    world::BoneMatrix out {};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            float sum = column == 3 ? a[row * 4 + 3] : 0.0f;
            for (size_t k = 0; k < 3; ++k) {
                sum += a[row * 4 + k] * b[k * 4 + column];
            }
            out[row * 4 + column] = sum;
        }
    }
    return out;
}

std::optional<world::BoneMatrix> invert(const world::BoneMatrix& m)
{
    const float a = m[0], b = m[1], c = m[2];
    const float d = m[4], e = m[5], f = m[6];
    const float g = m[8], h = m[9], i = m[10];
    float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(determinant) < 1.0e-8f) {
        return std::nullopt;
    }
    float s = 1.0f / determinant;
    world::BoneMatrix out {
        (e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s, 0.0f,
        (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s, 0.0f,
        (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s, 0.0f,
    };
    for (size_t row = 0; row < 3; ++row) {
        out[row * 4 + 3] = -(out[row * 4] * m[3] + out[row * 4 + 1] * m[7] + out[row * 4 + 2] * m[11]);
    }
    return out;
}

}

void Client::releaseSkinLayers(uint32_t slot)
{
    for (uint32_t layer = 0; layer < skinLayerOwners.size(); ++layer) {
        if (skinLayerOwners[layer] == slot + 1) {
            skinLayerOwners[layer] = 0;
            skinPixels.erase(layer);
            skinTileGrids.erase(layer);
        }
    }
    skinViews.erase(slot);
}

Client::SkinTexture Client::placeSkinTexture(uint32_t slot, const SkinImage& image, uint32_t maxTiles)
{
    SkinTexture placed;
    if (image.empty()) {
        return placed;
    }
    constexpr uint32_t Side = world::EntityTextureSize;
    uint32_t limit = std::clamp<uint32_t>(maxTiles, 1, world::MaxEntityTiles);
    uint32_t tilesX = std::clamp<uint32_t>((image.width + Side - 1) / Side, 1, limit);
    uint32_t tilesY = std::clamp<uint32_t>((image.height + Side - 1) / Side, 1, limit);
    auto freeRun = [&](uint32_t count) -> std::optional<uint32_t> {
        uint32_t run = 0;
        for (uint32_t layer = 0; layer < skinLayerOwners.size(); ++layer) {
            run = skinLayerOwners[layer] == 0 ? run + 1 : 0;
            if (run == count) {
                return layer + 1 - count;
            }
        }
        return std::nullopt;
    };
    std::optional<uint32_t> first = freeRun(tilesX * tilesY);
    if (!first) {
        tilesX = 1;
        tilesY = 1;
        first = freeRun(1);
    }
    if (!first) {
        return placed;
    }
    uint32_t spanX = tilesX * Side;
    uint32_t spanY = tilesY * Side;
    for (uint32_t tileY = 0; tileY < tilesY; ++tileY) {
        for (uint32_t tileX = 0; tileX < tilesX; ++tileX) {
            uint32_t layer = *first + tileY * tilesX + tileX;
            std::vector<uint8_t> pixels(size_t(Side) * Side * 4);
            for (uint32_t y = 0; y < Side; ++y) {
                size_t sourceY = size_t(tileY * Side + y) * image.height / spanY;
                for (uint32_t x = 0; x < Side; ++x) {
                    size_t sourceX = size_t(tileX * Side + x) * image.width / spanX;
                    std::memcpy(pixels.data() + (size_t(y) * Side + x) * 4, image.pixels.data() + (sourceY * image.width + sourceX) * 4, 4);
                }
            }
            skinLayerOwners[layer] = slot + 1;
            if (blockAssets) {
                renderer->updateEntityTexture(blockAssets->skinLayerBase() + layer, pixels.data());
            }
            skinPixels[layer] = std::move(pixels);
        }
    }
    placed.layer = *first;
    placed.grid = world::EntityTileGrid { tilesX, tilesY, 1.0f, 1.0f };
    placed.present = true;
    if (!placed.grid.single()) {
        skinTileGrids[placed.layer] = placed.grid;
    }
    return placed;
}

const Client::SkinView* Client::skinViewOf(uint32_t slot) const
{
    auto found = skinViews.find(slot);
    return found == skinViews.end() ? nullptr : &found->second;
}

world::EntityTileGrid Client::tileGridOf(uint32_t layer) const
{
    if (!blockAssets) {
        return {};
    }
    uint32_t base = blockAssets->skinLayerBase();
    if (layer >= base && layer < base + world::SkinPoolLayers) {
        auto found = skinTileGrids.find(layer - base);
        return found == skinTileGrids.end() ? world::EntityTileGrid {} : found->second;
    }
    return blockAssets->entityTileGrid(layer);
}

void Client::appendEntityQuad(const std::array<std::array<float, 3>, 4>& corners, const std::array<std::array<float, 2>, 4>& uvs, uint32_t layer, uint32_t shadeWord, std::vector<world::ModelQuadGpu>& out) const
{
    std::array<QuadCorner, 4> placed;
    for (size_t corner = 0; corner < 4; ++corner) {
        placed[corner].position = corners[corner];
        placed[corner].uv = uvs[corner];
    }
    appendTiled(placed, layer, tileGridOf(layer), shadeWord, out);
}

void Client::appendBeaconBeams(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended)
{
    if (!blockAssets) return;
    const double time = secondsNow() * TicksPerSecond;
    const float yaw = float(std::fmod(time, 160.0) * 2.25 - 45.0) * 3.14159265f / 180.0f;
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    for (const BeaconBeamView& beam : beaconBeamViews) {
        for (const world::BeaconBeamSection& section : beam.sections) {
            for (bool shell : { false, true }) {
                const auto layer = blockAssets->beaconBeamLayer(shell);
                if (!layer) continue;
                const std::array<std::array<float, 2>, 4> edge = shell
                    ? std::array<std::array<float, 2>, 4> { { { -0.25f, -0.25f }, { -0.25f, 0.25f }, { 0.25f, 0.25f }, { 0.25f, -0.25f } } }
                    : std::array<std::array<float, 2>, 4> { { { 0, 0.2f }, { -0.2f, 0 }, { 0, -0.2f }, { 0.2f, 0 } } };
                const float repeat = shell ? 1.0f : 2.5f;
                for (int32_t bottom = section.bottom; bottom < section.top; bottom += 4) {
                    const int32_t top = std::min(bottom + 4, section.top);
                    double scroll = -time * 0.2 + (bottom - beam.cell[1]) * repeat;
                    const float lowV = float(scroll - std::floor(scroll));
                    const float highV = lowV + (top - bottom) * repeat;
                    for (size_t side = 0; side < 4; ++side) {
                        auto point = [&](size_t edgeIndex, int32_t y) {
                            const auto& vertex = edge[edgeIndex & 3];
                            const float x = shell ? vertex[0] : vertex[0] * cosine - vertex[1] * sine;
                            const float z = shell ? vertex[1] : vertex[0] * sine + vertex[1] * cosine;
                            return std::array<float, 3> {
                                (float(beam.cell[0] - origin[0]) + 0.5f + x) * 256.0f,
                                float(y - origin[1]) * 256.0f,
                                (float(beam.cell[2] - origin[2]) + 0.5f + z) * 256.0f,
                            };
                        };
                        const std::array<QuadCorner, 4> corners { {
                            { point(side, bottom), { 0, lowV } }, { point(side + 1, bottom), { 1, lowV } },
                            { point(side + 1, top), { 1, highV } }, { point(side, top), { 0, highV } },
                        } };
                        auto gpu = packCorners(corners, *layer, EntityQuadFlag | FoggedQuadFlag | (1u << 9));
                        gpu.words[14] = 0x80000000u | section.color;
                        (shell ? blended : out).push_back(gpu);
                    }
                }
            }
        }
    }
}

/**
 * The quads of every entity close enough to the camera, placed around origin
 * in 1/256 block: players within 192 blocks, anything else within 72 blocks
 * on every axis, and only when its bounding box may be on screen. Animations
 * run once per game tick; each frame draws the bones blended between the
 * last two tick poses by the partial tick, then the model scaled and turned
 * to its body yaw. Each face takes the shade of the axis its posed normal is
 * closest to. Entity quads set bit 5 of the shade word so they sample the
 * entity textures, and bit 6 when their material adds light; the rest take
 * the light around the entity unless their render controller ignores
 * lighting. A render controller's uv_anim goes in words 14 and 15 as half
 * float offset and scale, wrapped per pixel. Quads whose material blends go
 * to blended. Zero scale entities draw nothing; invisible ones hide their
 * body but keep their armor and held item.
 */
void Client::buildActorQuads(const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& out, std::vector<world::ModelQuadGpu>& blended)
{
    actorDraws.clear();
    ++heldItemFrame;
    if (!blockAssets) {
        animators.clear();
        return;
    }
    double now = secondsNow();
    double worldTime = currentWorldTime(timeState);
    const size_t actualActors = actorViews.size();
    std::set<uint64_t> occupied;
    for (const ActorView& actor : actorViews) occupied.insert(actor.runtimeId);
    std::map<std::array<int32_t, 3>, SpawnerPose> shownSpawners;
    for (const SpawnerView& view : spawnerViews) {
        SpawnerPose pose;
        if (const auto found = spawnerPoses.find(view.cell); found != spawnerPoses.end()) pose = found->second;
        if (pose.identifier != view.display.identifier || occupied.contains(pose.id)) {
            if (pose.id) {
                animators.erase(pose.id);
                actorPoses.erase(pose.id);
                swimAmounts.erase(pose.id);
            }
            pose = {};
        }
        if (!pose.id) {
            pose.id = LocalActorId - 1;
            while (occupied.contains(pose.id)) --pose.id;
            pose.identifier = view.display.identifier;
            pose.time = now;
        }
        occupied.insert(pose.id);
        pose.spin = world::spawnerDisplaySpin(pose.spin, now - pose.time, view.display.delay);
        pose.time = now;
        shownSpawners.emplace(view.cell, pose);
        ActorView actor;
        actor.runtimeId = pose.id;
        actor.identifier = pose.identifier;
        actor.x = view.cell[0] + 0.5;
        actor.y = view.cell[1] + 0.4;
        actor.z = view.cell[2] + 0.5;
        actor.yaw = actor.headYaw = pose.spin;
        actor.width = view.display.width;
        actor.height = view.display.height;
        actor.scale = world::spawnerDisplayScale(view.display);
        actorViews.push_back(std::move(actor));
    }
    spawnerPoses = std::move(shownSpawners);
    double tickStart = playerView.tickTime;
    if (tickStart <= 0.0 || tickStart > now || now - tickStart > 2.0 / TicksPerSecond) {
        tickStart = std::floor(now * TicksPerSecond) / TicksPerSecond;
    }
    WorldView cullView;
    float aspect = static_cast<float>(window->width()) / static_cast<float>(std::max<uint32_t>(window->height(), 1));
    cullView.viewProjection = camera.viewProjection(aspect);
    cullView.cameraX = camera.x();
    cullView.cameraY = camera.y();
    cullView.cameraZ = camera.z();
    ChunkFrustum frustum(cullView);
    bool facing = camera.isFacingSubject();
    float viewYaw = wrapDegrees(camera.minecraftYaw() + (facing ? 180.0f : 0.0f));
    float viewPitch = facing ? -camera.minecraftPitch() : camera.minecraftPitch();
    auto& present = actorPresent;
    present.clear();
    if (present.bucket_count() * present.max_load_factor() < actorViews.size()) present.reserve(actorViews.size());
    for (const ActorView& actor : actorViews) {
        present.insert(actor.runtimeId);
        const double actorTickStart = world::projectileEntity(actor.identifier) && actor.projectileTickTime > 0.0 ? actor.projectileTickTime : tickStart;
        const float actorPartialTick = float(std::clamp((now - actorTickStart) * TicksPerSecond, 0.0, 1.0));
        if (actor.scale <= 0.0f) {
            continue;
        }
        bool invisible = (actor.flags[0] & InvisibleFlag) != 0;
        double dx = actor.x - origin[0];
        double dy = actor.y - origin[1];
        double dz = actor.z - origin[2];
        bool player = actor.identifier == "minecraft:player";
        std::array<double, 3> fromCamera { actor.x - camera.x(), actor.y + 1.0 - camera.y(), actor.z - camera.z() };
        if (player) {
            if (fromCamera[0] * fromCamera[0] + fromCamera[1] * fromCamera[1] + fromCamera[2] * fromCamera[2] > MaxPlayerDistance * MaxPlayerDistance) {
                continue;
            }
        } else if (std::abs(actor.x - camera.x()) > ActorCandidateRadius || std::abs(actor.y - camera.y()) > ActorCandidateRadius || std::abs(actor.z - camera.z()) > ActorCandidateRadius) {
            continue;
        }
        float boxWidth = actorExtent(actor.width, player ? PlayerWidth : DefaultActorWidth, actor.scale);
        float boxHeight = actorExtent(actor.height, player ? PlayerHeight : DefaultActorHeight, actor.scale);
        float halfWidth = boxWidth * 0.5f + PoseMargin;
        float halfHeight = boxHeight * 0.5f + PoseMargin;
        // Collision bounds need not enclose custom skins or animated geometry.
        // Outside that box, reject individual posed faces rather than the actor.
        bool cullFaces = !frustum.containsBox(cullView, actor.x, actor.y + boxHeight * 0.5, actor.z, halfWidth, halfHeight, halfWidth);
        uint32_t light = lightCorners(actor.x, actor.y + boxHeight * 0.66f, actor.z);
        if (actor.identifier == "minecraft:item") {
            if (!invisible) {
                size_t first = out.size();
                appendDroppedItem(actor, origin, now, out);
                lightQuads(out, first, light);
            }
            continue;
        }
        if (actor.identifier == "minecraft:falling_block") {
            if (!invisible && !actor.fallingBlockLanded) {
                size_t first = out.size(), firstBlended = blended.size();
                appendFallingBlock(actor, origin, out, blended);
                lightQuads(out, first, light);
                lightQuads(blended, firstBlended, light);
            }
            continue;
        }
        if (actor.identifier == "minecraft:tnt") {
            if (!invisible) {
                if (const world::BlockVisual* visual = blockAssets->itemCube("minecraft:tnt")) {
                    size_t first = out.size(), firstBlended = blended.size();
                    appendActorBlock(actor, *visual, origin, out, blended);
                    lightQuads(out, first, light);
                    lightQuads(blended, firstBlended, light);
                }
            }
            continue;
        }
        const world::EntityModel* model = actor.slim ? blockAssets->entityModel(actor.identifier + "#slim") : nullptr;
        if (!model) {
            model = blockAssets->entityModel(actor.identifier);
        }
        if (!model) {
            continue;
        }
        world::EntityAnimator& animator = animators[actor.runtimeId];
        world::AnimationInput input;
        input.x = actor.x;
        input.y = actor.y;
        input.z = actor.z;
        input.yaw = actor.yaw;
        input.headYaw = actor.headYaw;
        input.pitch = actor.pitch;
        input.now = actorTickStart;
        input.hurtTime = actor.lastHurt > 0.0 ? static_cast<float>(std::clamp(10.0 - (now - actor.lastHurt) * 20.0, 0.0, 10.0)) : 0.0f;
        input.worldTime = worldTime;
        input.flags = actor.flags;
        input.variant = actor.variant;
        input.markVariant = actor.markVariant;
        input.color = actor.color;
        input.health = actor.health;
        input.maxHealth = actor.maxHealth;
        input.deathTicks = actor.diedAt > 0.0 ? float(std::max(now - actor.diedAt, 0.0) * TicksPerSecond) : 0.0f;
        input.onFireTime = actor.fireChangedAt > 0.0 ? float(std::max(now - actor.fireChangedAt, 0.0) * TicksPerSecond) : 0.0f;
        input.horseFlags = actor.horseFlags;
        input.nativeVelocity = actor.velocity;
        input.metadataQueries = actor.animationQueries;
        input.properties = actor.animationProperties;
        input.skinId = actor.skinId;
        input.identifier = actor.identifier;
        input.name = actor.name;
        if (!player && (actor.name == "Dinnerbone" || actor.name == "Grumm") && actor.animationQueries.contains("has_health")
            && actor.identifier.find("minecart") == std::string::npos && actor.identifier.find("boat") == std::string::npos && !actor.identifier.ends_with(":raft") && !actor.identifier.ends_with("_raft")) {
            float height = std::isfinite(actor.height) && actor.height > 0.0f ? actor.height : DefaultActorHeight;
            input.metadataQueries["upside_down_height"] = height * 16.0;
        }
        input.onGround = actor.onGround;
        if (world::projectileEntity(actor.identifier)) {
            input.tickPositionDelta = actor.projectilePositionDelta;
            input.frameAlpha = actorPartialTick;
            input.shakeTime = actor.projectileShakeTicks;
            input.attachedToEntity = actor.fireworkShooterId != -1;
            if (actor.projectileTickTime > 0.0) {
                input.x = actor.projectilePrevious[0] + actor.projectilePositionDelta[0];
                input.y = actor.projectilePrevious[1] + actor.projectilePositionDelta[1];
                input.z = actor.projectilePrevious[2] + actor.projectilePositionDelta[2];
                input.yaw = actor.projectileCurrentTurn[0];
                input.headYaw = actor.projectileCurrentTurn[1];
                input.pitch = actor.projectileCurrentTurn[2];
                input.now = actorTickStart;
            }
        }
        if (seenSessionSnapshot) input.inWater = session.cameraEnvironment(*seenSessionSnapshot, { actor.x, actor.y + 0.1, actor.z }, false).first == 1;
        input.cameraX = camera.x();
        input.cameraY = camera.y();
        input.cameraZ = camera.z();
        input.cameraYaw = viewYaw;
        input.cameraPitch = viewPitch;
        if (actor.identifier == "minecraft:armor_stand") {
            input.engineVariables = { { "armor_stand.pose_index", double(actor.poseIndex) } };
        }
        input.randomSeed = actor.runtimeId;
        input.offHandItem = actor.runtimeId == LocalActorId ? hudState.offhand.identifier : actor.offhand.identifier;
        const auto& armorItems = actor.runtimeId == LocalActorId ? hudState.armor : actor.armorItems;
        for (size_t slot = 0; slot < armorItems.size(); ++slot) {
            input.armorItems[slot] = armorItems[slot].identifier;
            input.armorColors[slot] = armorItems[slot].customColor;
            input.armorDamage[slot] = armorItems[slot].damage;
        }
        input.armorItems[4] = actor.bodyArmor.identifier;
        input.armorColors[4] = actor.bodyArmor.customColor;
        input.armorDamage[4] = actor.bodyArmor.damage;
        input.itemUseTicks = actorItemUseTicks(actor, now);
        if (input.itemUseTicks > 0.0) {
            input.flags[0] |= UsingItemFlag;
        }
        if (actor.runtimeId == LocalActorId) {
            const HudItem& held = hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))];
            input.mainHandItem = held.empty() ? std::string() : held.identifier;
            input.engineVariables = {
                { "attack_time", swingProgress() },
                { "is_holding_right", held.empty() ? 0.0 : 1.0 },
                { "is_first_person", 0.0 },
            };
            if (activeEmote) {
                // Leaving the clip out for a frame starts it over when the same emote is picked again.
                if (activeEmote->restartFrames > 0) {
                    --activeEmote->restartFrames;
                } else {
                    input.extraAnimations.push_back(activeEmote->clip);
                    input.extraLibrary = &emoteAnimations;
                }
            }
        } else {
            input.mainHandItem = actor.held.empty() ? std::string() : actor.held.identifier;
            input.engineVariables.push_back({ "is_holding_right", actor.held.empty() ? 0.0 : 1.0 });
            if (actor.lastSwing > 0.0) {
                input.engineVariables.push_back({ "attack_time", swingProgressSince(actor.lastSwing, now) });
            }
        }
        bool swimmingFlag = (actor.flags[SwimmingFlag / 64] >> (SwimmingFlag % 64)) & 1;
        float& swimAmount = swimAmounts.try_emplace(actor.runtimeId, swimmingFlag ? 1.0f : 0.0f).first->second;
        float swimStep = static_cast<float>(std::clamp(now - lastActorTime, 0.0, 0.25) * 4.0);
        swimAmount = std::clamp(swimAmount + (swimmingFlag ? swimStep : -swimStep), 0.0f, 1.0f);
        input.swimAmount = swimAmount;
        input.engineVariables.push_back({ "swim_amount", swimAmount });
        input.engineVariables.push_back({ "left_arm_swim_amount", swimAmount });
        input.engineVariables.push_back({ "right_arm_swim_amount", swimAmount });
        const SkinView* skinView = actor.skinSlot != NoSkin ? skinViewOf(actor.skinSlot) : nullptr;
        if (skinView) {
            for (const SkinAnimationView& animation : skinView->animations) {
                input.engineVariables.push_back({ skinFramesVariable(animation.kind), double(animation.frames) });
                if (animation.kind == SkinAnimationKind::Face) {
                    input.engineVariables.push_back({ "use_blinking_animation", animation.blinking ? 1.0 : 0.0 });
                    input.extraAnimations.push_back(PersonaBlinkAlias);
                }
            }
        }
        if (model->rigs.empty()) {
            continue;
        }
        bool combined = !model->combined.quads.empty() && actor.skinSlot == NoSkin;
        const world::EntityRenderController* controller = nullptr;
        for (const world::EntityRenderController& candidate : model->controllers) {
            if (candidate.condition.empty() || animator.evaluate(candidate.condition) != 0.0) {
                controller = &candidate;
                break;
            }
        }
        uint32_t rigIndex = controller ? pickChoice(animator, controller->geometry, controller->geometryChoices) : 0;
        const world::EntityRig* chosenRig = &model->rigs[rigIndex < model->rigs.size() ? rigIndex : 0];
        const world::EntityRig* geometryRig = chosenRig;
        if (combined) {
            chosenRig = &model->combined;
        } else if (actor.skinSlot != NoSkin) {
            if (auto skinRig = skinRigs.find(actor.skinSlot); skinRig != skinRigs.end() && skinRig->second) {
                chosenRig = skinRig->second.get();
            }
        }
        const world::EntityRig& rig = *chosenRig;
        ActorPose& pose = actorPoses[actor.runtimeId];
        const world::EntityRig* poseSource = combined ? geometryRig : chosenRig;
        bool geometryChanged = pose.source != poseSource;
        if (geometryChanged) {
            pose.source = poseSource;
            if (combined) pose.bones = world::poseBonesForGeometry(rig.bones, geometryRig->bones);
        }
        const auto& animationBones = combined ? pose.bones : rig.bones;
        bool stale = geometryChanged || animator.matrices().size() != rig.bones.size() || pose.current.size() != rig.bones.size() || actorTickStart - pose.tick > 3.0 / TicksPerSecond;
        bool billboard = world::cameraFacingSprite(actor.identifier);
        if (stale || pose.tick != actorTickStart || billboard) {
            Profiler::Section section(profiler, "  animation");
            if (billboard) input.now = now;
            animator.update(model->scripts.get(), &blockAssets->animationLibrary(), animationBones, input);
            pose.previous = stale || billboard ? animator.matrices() : std::move(pose.current);
            pose.current = animator.matrices();
            pose.tick = actorTickStart;
        }
        animator.setRenderContext(input, now, actorPartialTick);
        if (!invisible && !cullFaces && actor.identifier == "minecraft:ender_crystal") {
            if (auto layer = blockAssets->crystalBeamLayer()) {
                double age = animator.evaluate(lifeTimeScript());
                float scroll = float(std::fmod(std::max(age, 0.0) * TicksPerSecond * 0.01, 1.0));
                world::crystalBeamQuads({ actor.x, actor.y, actor.z }, actor.crystalBeamTarget,
                    [&](const world::CrystalBeamQuad& quad, float offset) {
                        std::array<QuadCorner, 4> corners;
                        uint32_t brightness = 0;
                        for (size_t corner = 0; corner < 4; ++corner) {
                            for (size_t axis = 0; axis < 3; ++axis) {
                                double position = (axis == 0 ? dx : axis == 1 ? dy : dz) + quad.positions[corner][axis];
                                if (std::abs(position) > 32700.0) return;
                                corners[corner].position[axis] = float(position * 256.0);
                            }
                            corners[corner].uv = quad.uvs[corner];
                            brightness |= uint32_t(std::lround(quad.brightness[corner] * 255.0f)) << (corner * 8);
                        }
                        auto gpu = packCorners(corners, *layer, EntityQuadFlag | FoggedQuadFlag | (1u << 13));
                        gpu.words[12] = brightness;
                        gpu.words[14] = uint32_t(toHalf(std::fmod(offset + scroll, 1.0f))) << 16;
                        gpu.words[15] = uint32_t(toHalf(1.0f)) | (uint32_t(toHalf(1.0f)) << 16);
                        out.push_back(gpu);
                    });
            }
        }
        std::vector<world::BoneMatrix>& matrices = pose.interpolated;
        bool interpolated = false;
        auto interpolatePose = [&] {
            if (interpolated) return;
            interpolated = true;
            matrices = pose.current;
            if (pose.previous.size() != matrices.size()) return;
            for (size_t bone = 0; bone < matrices.size(); ++bone) {
                for (size_t cell = 0; cell < matrices[bone].size(); ++cell) {
                    float from = pose.previous[bone][cell];
                    matrices[bone][cell] = from + (matrices[bone][cell] - from) * actorPartialTick;
                }
            }
        };
        float scale = animator.scale() * actor.scale * 16.0f;
        auto hiddenBones = [&](const world::EntityRenderController& source) {
            auto& hidden = actorPartHidden;
            hidden.clear();
            if (source.parts.empty()) return std::cref(hidden);
            auto& visible = actorPartVisible;
            visible.assign(source.parts.size(), 1);
            for (size_t rule = 0; rule < source.parts.size(); ++rule) {
                visible[rule] = animator.evaluate(source.parts[rule].visible) != 0.0 ? 1 : 0;
            }
            std::vector<int32_t>& lastRule = partMatches[{ &source, &rig }];
            if (lastRule.size() != rig.bones.size()) {
                lastRule.assign(rig.bones.size(), -1);
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    for (size_t rule = 0; rule < source.parts.size(); ++rule) {
                        if (matchesPattern(source.parts[rule].pattern, rig.bones[bone].name)) {
                            lastRule[bone] = static_cast<int32_t>(rule);
                        }
                    }
                }
            }
            auto& own = actorPartOwn;
            own.assign(rig.bones.size(), 0);
            for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                if (lastRule[bone] >= 0) {
                    own[bone] = visible[lastRule[bone]] ? 0 : 1;
                }
            }
            hidden.assign(rig.bones.size(), 0);
            actorPartState.assign(rig.bones.size(), 0);
            for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                if (actorPartState[bone] == 2) continue;
                actorPartPath.clear();
                int32_t walker = static_cast<int32_t>(bone);
                while (walker >= 0 && size_t(walker) < rig.bones.size() && actorPartState[size_t(walker)] == 0) {
                    actorPartState[size_t(walker)] = 1;
                    actorPartPath.push_back(size_t(walker));
                    walker = animationBones[size_t(walker)].parent;
                }
                uint8_t inherited = walker >= 0 && size_t(walker) < rig.bones.size() && actorPartState[size_t(walker)] == 2 ? hidden[size_t(walker)] : 0;
                for (auto it = actorPartPath.rbegin(); it != actorPartPath.rend(); ++it) {
                    inherited |= own[*it];
                    hidden[*it] = inherited;
                    actorPartState[*it] = 2;
                }
            }
            return std::cref(hidden);
        };
        auto textureOf = [&](const world::EntityRenderController* source) {
            uint32_t chosen = source ? pickChoice(animator, source->texture, source->textureChoices) : world::NoEntityChoice;
            return chosen != world::NoEntityChoice ? chosen : model->layer;
        };
        float radians = (180.0f - world::entityModelYaw(actor.identifier, actor.yaw)) * 3.14159265f / 180.0f;
        float cosine = std::cos(radians);
        float sine = std::sin(radians);
        std::optional<std::array<float, 2>> glide = glideRotation(actor, now, actorPartialTick);
        auto tilt = [&](float& x, float& y, float& z) {
            if (!glide) {
                return;
            }
            constexpr float Radians = 0.017453292f;
            float bankSine = std::sin((*glide)[1] * Radians);
            float bankCosine = std::cos((*glide)[1] * Radians);
            float turnedX = bankCosine * x + bankSine * z;
            float turnedZ = -bankSine * x + bankCosine * z;
            float pitchSine = std::sin((*glide)[0] * Radians);
            float pitchCosine = std::cos((*glide)[0] * Radians);
            float pitchedY = pitchCosine * y - pitchSine * turnedZ;
            float pitchedZ = pitchSine * y + pitchCosine * turnedZ;
            x = turnedX;
            y = pitchedY;
            z = pitchedZ;
        };
        float baseX = static_cast<float>(dx * 256.0);
        float baseY = static_cast<float>(dy * 256.0);
        float baseZ = static_cast<float>(dz * 256.0);
        std::array<float, 3> cameraLocal {
            static_cast<float>((camera.x() - origin[0]) * 256.0),
            static_cast<float>((camera.y() - origin[1]) * 256.0),
            static_cast<float>((camera.z() - origin[2]) * 256.0),
        };
        auto uvAnimOf = [&](const world::EntityRenderController* source) {
            std::array<uint32_t, 2> words {};
            if (!source || !source->uvAnimated) {
                return words;
            }
            std::array<float, 4> values { 0.0f, 0.0f, 1.0f, 1.0f };
            for (size_t slot = 0; slot < values.size(); ++slot) {
                double value = animator.evaluate(source->uvAnim[slot]);
                if (std::isfinite(value)) {
                    values[slot] = static_cast<float>(value);
                }
            }
            if (values[0] == 0.0f && values[1] == 0.0f && values[2] == 1.0f && values[3] == 1.0f) {
                return words;
            }
            words[0] = uint32_t(toHalf(values[0] - std::floor(values[0]))) | (uint32_t(toHalf(values[1] - std::floor(values[1]))) << 16);
            words[1] = uint32_t(toHalf(values[2])) | (uint32_t(toHalf(values[3])) << 16);
            return words;
        };
        auto wornBones = [&](const world::EntityRig& worn, const world::EntityRig& wearer) -> const std::vector<int32_t>& {
            std::vector<int32_t>& matched = armorBoneMatches[{ &worn, &wearer }];
            if (matched.size() == worn.bones.size()) {
                return matched;
            }
            int32_t body = -1;
            for (size_t bone = 0; bone < wearer.bones.size(); ++bone) {
                if (lowercase(wearer.bones[bone].name) == "body") {
                    body = static_cast<int32_t>(bone);
                }
            }
            matched.assign(worn.bones.size(), -1);
            for (size_t piece = 0; piece < worn.bones.size(); ++piece) {
                std::string name = lowercase(worn.bones[piece].name);
                for (size_t bone = 0; bone < wearer.bones.size(); ++bone) {
                    if (lowercase(wearer.bones[bone].name) == name) {
                        matched[piece] = static_cast<int32_t>(bone);
                        break;
                    }
                }
                if (matched[piece] < 0 && name == "cape") {
                    matched[piece] = body;
                }
            }
            return matched;
        };
        const world::EntityRenderController* renderingController = nullptr;
        world::EntityMaterialChoice renderingMaterial;
        std::array<float, 36> surfaceConstants {};
        auto emitQuad = [&](const world::ModelQuad& quad, const world::BoneMatrix* matrix, uint32_t layer, world::EntityBlend blend, bool oneSided, bool lit, const std::array<uint32_t, 2>& uvAnim, float offsetV) {
            auto place = [&](const std::array<float, 3>& point) {
                float x = point[0];
                float y = point[1];
                float z = point[2];
                if (matrix) {
                    const world::BoneMatrix& m = *matrix;
                    float px = m[0] * x + m[1] * y + m[2] * z + m[3];
                    float py = m[4] * x + m[5] * y + m[6] * z + m[7];
                    float pz = m[8] * x + m[9] * y + m[10] * z + m[11];
                    x = px;
                    y = py;
                    z = pz;
                }
                x *= scale;
                y *= scale;
                z *= scale;
                tilt(x, y, z);
                return std::array<float, 3> { baseX + cosine * x + sine * z, baseY + y, baseZ - sine * x + cosine * z };
            };
            std::array<QuadCorner, 4> corners;
            std::array<float, 3> center {};
            for (size_t corner = 0; corner < 4; ++corner) {
                std::array<float, 3> point { quad.positions[corner][0] / 16.0f, quad.positions[corner][1] / 16.0f, quad.positions[corner][2] / 16.0f };
                for (size_t axis = 0; axis < 3; ++axis) {
                    center[axis] += point[axis] * 0.25f;
                }
                corners[corner].position = place(point);
                corners[corner].uv = { quad.uvs[corner][0] / 4096.0f, quad.uvs[corner][1] / 4096.0f + offsetV };
            }
            if (cullFaces) {
                auto minimum = corners[0].position;
                auto maximum = minimum;
                for (size_t corner = 1; corner < corners.size(); ++corner) {
                    for (size_t axis = 0; axis < 3; ++axis) {
                        minimum[axis] = std::min(minimum[axis], corners[corner].position[axis]);
                        maximum[axis] = std::max(maximum[axis], corners[corner].position[axis]);
                    }
                }
                if (!frustum.containsBox(cullView,
                        origin[0] + (double(minimum[0]) + maximum[0]) / 512.0,
                        origin[1] + (double(minimum[1]) + maximum[1]) / 512.0,
                        origin[2] + (double(minimum[2]) + maximum[2]) / 512.0,
                        (maximum[0] - minimum[0]) / 512.0f,
                        (maximum[1] - minimum[1]) / 512.0f,
                        (maximum[2] - minimum[2]) / 512.0f)) {
                    return;
                }
            }
            bool inward = (quad.flags & world::QuadInward) != 0;
            if (inward || oneSided || !(quad.flags & world::QuadTwoSided)) {
                std::array<float, 3> edgeA {}, edgeB {}, toCamera {};
                for (size_t axis = 0; axis < 3; ++axis) {
                    edgeA[axis] = corners[1].position[axis] - corners[0].position[axis];
                    edgeB[axis] = corners[3].position[axis] - corners[0].position[axis];
                    toCamera[axis] = cameraLocal[axis] - corners[0].position[axis];
                }
                float facing = (edgeA[1] * edgeB[2] - edgeA[2] * edgeB[1]) * toCamera[0] + (edgeA[2] * edgeB[0] - edgeA[0] * edgeB[2]) * toCamera[1] + (edgeA[0] * edgeB[1] - edgeA[1] * edgeB[0]) * toCamera[2];
                // Quads wind with their normal into the cube, so an outer face shows
                // when the camera sits on the other side of it.
                if (inward ? facing <= 0.0f : facing >= 0.0f) {
                    return;
                }
            }
            uint32_t shadeWord = world::posedShadeFace(quad.flags & world::QuadFaceMask, center, 1.0f, place) | EntityQuadFlag | (blend == world::EntityBlend::Additive ? AdditiveQuadFlag : 0u);
            if (!lit) shadeWord |= FoggedQuadFlag;
            if (renderingMaterial.material == world::EntityMaterial::AlphaTest) shadeWord |= 1u << 14;
            if (actor.lastHurt > 0.0 && now - actor.lastHurt < 0.5) shadeWord |= 1u << 7;
            std::vector<world::ModelQuadGpu>& target = blend == world::EntityBlend::Opaque ? out : blended;
            size_t first = target.size();
            world::EntityTileGrid grid = tileGridOf(layer);
            appendTiled(corners, layer, grid, shadeWord, target);
            if (grid.single() && uvAnim[1] != 0) {
                for (size_t placed = first; placed < target.size(); ++placed) {
                    target[placed].words[14] = uvAnim[0];
                    target[placed].words[15] = uvAnim[1];
                }
            }
            if (uvAnim[1] == 0 && renderingController && !player) {
                uint32_t rgb = 0;
                for (size_t channel = 0; channel < 3; ++channel) rgb |= uint32_t(std::clamp(surfaceConstants[8 + channel], 0.0f, 1.0f) * 255.0f) << (16 - channel * 8);
                for (size_t placed = first; placed < target.size(); ++placed) target[placed].words[14] = 0x80000000u | rgb;
            }
            if (lit) {
                lightQuads(target, first, light);
            }
        };
        pose.gpuTransformValid.assign(rig.bones.size() + 1, 0);
        pose.gpuTransforms.resize(rig.bones.size() + 1);
        auto setSurface = [&](const world::EntityRenderController* controller, uint32_t layer) {
            renderingController = controller;
            renderingMaterial = controller ? controller->selectedMaterial(animator.evaluate(controller->materialSelector)) : world::EntityMaterialChoice {};
            surfaceConstants.fill(0.0f);
            surfaceConstants[4] = surfaceConstants[5] = surfaceConstants[6] = surfaceConstants[7] = 1.0f;
            surfaceConstants[8] = surfaceConstants[9] = surfaceConstants[10] = surfaceConstants[11] = 1.0f;
            bool hurt = input.hurtTime > 0.0f || input.deathTicks > 0.0f;
            if (hurt) {
                surfaceConstants[12] = 1.0f;
                surfaceConstants[15] = 0.5f;
                if (visuals.hitColor) {
                    const auto& color = *visuals.hitColor;
                    surfaceConstants[12] = color.r / 255.0f;
                    surfaceConstants[13] = color.g / 255.0f;
                    surfaceConstants[14] = color.b / 255.0f;
                    surfaceConstants[15] = std::max<uint8_t>(color.a, 1) / 255.0f;
                }
            }
            if (controller) {
                for (size_t channel = 0; channel < 4; ++channel) {
                    surfaceConstants[8 + channel] = float(animator.evaluateWithThis(controller->color[channel], 1.0));
                    surfaceConstants[12 + channel] = float(animator.evaluateWithThis(controller->overlay[channel], surfaceConstants[12 + channel]));
                }
                if (hurt) {
                    for (size_t channel = 0; channel < 4; ++channel) surfaceConstants[12 + channel] = float(animator.evaluateWithThis(controller->hurtColor[channel], surfaceConstants[12 + channel]));
                } else if ((actor.flags[0] & 1u) != 0u || (actor.fireChangedAt > 0.0 && input.onFireTime < 10.0f)) {
                    for (size_t channel = 0; channel < 4; ++channel) surfaceConstants[12 + channel] = float(animator.evaluateWithThis(controller->fireColor[channel], surfaceConstants[12 + channel]));
                }
            }
            surfaceConstants[4] = surfaceConstants[5] = 0.0f;
            if (controller && controller->uvAnimated) {
                for (size_t channel = 0; channel < 4; ++channel) surfaceConstants[4 + channel] = float(animator.evaluateWithThis(controller->uvAnim[channel], surfaceConstants[4 + channel]));
            }
            std::array<uint32_t, 3> textures { layer, world::NoEntityChoice, world::NoEntityChoice };
            if (controller) for (size_t index = 0; index < 2; ++index) textures[index + 1] = pickChoice(animator, controller->extraTextures[index], controller->extraTextureChoices[index]);
            for (size_t index = 0; index < 3; ++index) {
                surfaceConstants[16 + index] = std::bit_cast<float>(textures[index]);
                auto grid = tileGridOf(textures[index] == world::NoEntityChoice ? layer : textures[index]);
                surfaceConstants[20 + index * 4] = float(grid.tilesX);
                surfaceConstants[21 + index * 4] = float(grid.tilesY);
                surfaceConstants[22 + index * 4] = grid.coverX;
                surfaceConstants[23 + index * 4] = grid.coverY;
            }
            surfaceConstants[19] = std::bit_cast<float>(uint32_t(renderingMaterial.material));
            surfaceConstants[35] = surfaceConstants[15];
        };
        auto emitGpu = [&](size_t index, size_t bone, uint32_t layer, bool lit, bool oneSided, world::EntityBlend blend) {
            auto& cached = actorGeometry[&rig];
            if (!cached.id) {
                cached.id = nextActorGeometry++;
                cached.quads.resize(rig.quads.size());
                cached.bounds.assign(rig.bones.size() + 1, { 32768.0f, 32768.0f, 32768.0f, -32768.0f, -32768.0f, -32768.0f });
                for (size_t q = 0; q < rig.quads.size(); ++q) {
                    const auto& source = rig.quads[q];
                    size_t owner = q < rig.quadBones.size() ? std::min<size_t>(rig.quadBones[q], rig.bones.size()) : rig.bones.size();
                    auto& bounds = cached.bounds[owner];
                    for (const auto& position : source.positions) for (size_t axis = 0; axis < 3; ++axis) {
                        bounds[axis] = std::min(bounds[axis], float(position[axis]) / 16.0f);
                        bounds[axis + 3] = std::max(bounds[axis + 3], float(position[axis]) / 16.0f);
                    }
                    auto& words = cached.quads[q].words;
                    for (size_t component = 0; component < 12; ++component) {
                        uint32_t value = uint16_t(source.positions[component / 3][component % 3]);
                        words[component / 2] |= value << ((component % 2) * 16);
                    }
                    for (size_t corner = 0; corner < 4; ++corner)
                        words[6 + corner] = uint32_t(source.uvs[corner][0]) | (uint32_t(source.uvs[corner][1]) << 16);
                    words[11] = source.flags & world::QuadFaceMask;
                    std::array<float, 3> ab {}, ac {}, normal {};
                    for (size_t axis = 0; axis < 3; ++axis) {
                        ab[axis] = float(source.positions[1][axis] - source.positions[0][axis]);
                        ac[axis] = float(source.positions[3][axis] - source.positions[0][axis]);
                    }
                    normal = { ac[1] * ab[2] - ac[2] * ab[1], ac[2] * ab[0] - ac[0] * ab[2], ac[0] * ab[1] - ac[1] * ab[0] };
                    float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                    if (length > 0.0f) for (float& value : normal) value /= length;
                    for (size_t axis = 0; axis < 3; ++axis) words[12 + axis] = std::bit_cast<uint32_t>(normal[axis]);
                }
            }
            cached.used = heldItemFrame;
            size_t slot = bone < pose.current.size() ? bone : rig.bones.size();
            auto& transform = pose.gpuTransforms[slot];
            if (!pose.gpuTransformValid[slot]) {
                constexpr world::BoneMatrix identity { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
                const float factor = scale / 256.0f;
                for (size_t state = 0; state < 2; ++state) {
                    const auto& bones = state ? pose.current : pose.previous;
                    const auto& m = bone < bones.size() ? bones[bone] : identity;
                    for (size_t column = 0; column < 4; ++column) {
                        transform[state * 12 + column] = factor * (cosine * m[column] + sine * m[8 + column]);
                        transform[state * 12 + 4 + column] = factor * m[4 + column];
                        transform[state * 12 + 8 + column] = factor * (-sine * m[column] + cosine * m[8 + column]);
                    }
                    transform[state * 12 + 3] += float(dx);
                    transform[state * 12 + 7] += float(dy);
                    transform[state * 12 + 11] += float(dz);
                }
                pose.gpuTransformValid[slot] = 1;
                if (cullFaces) {
                    const auto& bounds = cached.bounds[slot];
                    std::array<float, 3> center {}, extent {};
                    for (size_t row = 0; row < 3; ++row) {
                        center[row] = transform[row * 4 + 3] + (transform[12 + row * 4 + 3] - transform[row * 4 + 3]) * actorPartialTick;
                        for (size_t column = 0; column < 3; ++column) {
                            float coefficient = transform[row * 4 + column] + (transform[12 + row * 4 + column] - transform[row * 4 + column]) * actorPartialTick;
                            center[row] += coefficient * (bounds[column] + bounds[column + 3]) * 0.5f;
                            extent[row] += std::abs(coefficient) * (bounds[column + 3] - bounds[column]) * 0.5f;
                        }
                    }
                    if (!frustum.containsBox(cullView, origin[0] + center[0], origin[1] + center[1], origin[2] + center[2], extent[0], extent[1], extent[2])) pose.gpuTransformValid[slot] = 2;
                }
            }
            if (pose.gpuTransformValid[slot] == 2) return;
            if (oneSided) {
                auto point = [&](size_t corner) {
                    std::array<float, 3> placed {};
                    for (size_t axis = 0; axis < 3; ++axis) {
                        for (size_t column = 0; column < 4; ++column) {
                            float coefficient = transform[axis * 4 + column] + (transform[12 + axis * 4 + column] - transform[axis * 4 + column]) * actorPartialTick;
                            placed[axis] += coefficient * (column == 3 ? 1.0f : rig.quads[index].positions[corner][column] / 16.0f);
                        }
                    }
                    return placed;
                };
                const auto a = point(0), b = point(1), c = point(3);
                std::array<float, 3> ab {}, ac {}, toCamera {};
                for (size_t axis = 0; axis < 3; ++axis) {
                    ab[axis] = b[axis] - a[axis]; ac[axis] = c[axis] - a[axis];
                    toCamera[axis] = cameraLocal[axis] / 256.0f - a[axis];
                }
                if ((ab[1] * ac[2] - ab[2] * ac[1]) * toCamera[0]
                    + (ab[2] * ac[0] - ab[0] * ac[2]) * toCamera[1]
                    + (ab[0] * ac[1] - ab[1] * ac[0]) * toCamera[2] >= 0.0f) return;
            }
            ActorDraw draw;
            draw.geometryKey = cached.id;
            draw.quads = cached.quads.data();
            draw.total = uint32_t(cached.quads.size());
            draw.first = uint32_t(index);
            draw.count = 1;
            std::copy(transform.begin(), transform.end(), draw.constants.begin());
            std::copy(surfaceConstants.begin(), surfaceConstants.end(), draw.constants.begin() + 24);
            draw.constants[24] = actorPartialTick;
            draw.constants[25] = std::bit_cast<float>(layer);
            uint32_t flags = (blend != world::EntityBlend::Opaque ? (1u << 12) : 0u) | EntityQuadFlag | (1u << 11) | (lit ? (1u << 8) : FoggedQuadFlag) | (blend == world::EntityBlend::Additive ? AdditiveQuadFlag : 0u);
            if (actor.lastHurt > 0.0 && now - actor.lastHurt < 0.5) flags |= 1u << 7;
            draw.constants[26] = std::bit_cast<float>(flags);
            draw.constants[27] = std::bit_cast<float>(light);
            draw.blended = blend != world::EntityBlend::Opaque;
            draw.depthOnly = renderingController && renderingController->material == world::EntityMaterial::DissolveDepth;
            draw.equalDepth = renderingController && renderingController->material == world::EntityMaterial::DissolveColor;
            if (draw.blended) for (size_t axis = 0; axis < 3; ++axis) {
                float center = 0.0f;
                for (size_t corner = 0; corner < 4; ++corner) {
                    for (size_t column = 0; column < 4; ++column) {
                        float coefficient = transform[axis * 4 + column] + (transform[12 + axis * 4 + column] - transform[axis * 4 + column]) * actorPartialTick;
                        center += coefficient * (column == 3 ? 1.0f : rig.quads[index].positions[corner][column] / 16.0f) * 0.25f;
                    }
                }
                draw.center[axis] = center;
            }
            if (!actorDraws.empty()) {
                auto& previous = actorDraws.back();
                if (!draw.blended && previous.geometryKey == draw.geometryKey && previous.first + previous.count == draw.first
                    && std::memcmp(previous.constants.data(), draw.constants.data(), sizeof(draw.constants)) == 0) {
                    ++previous.count;
                    return;
                }
            }
            actorDraws.push_back(draw);
        };
        auto emit = [&](size_t index, uint32_t layer, const std::vector<uint8_t>& hidden, world::EntityBlend blend, bool oneSided, bool lit, const std::array<uint32_t, 2>& uvAnim) {
            const world::ModelQuad& quad = rig.quads[index];
            size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : pose.current.size();
            if (bone < hidden.size() && hidden[bone]) {
                return;
            }
            if (!player && !(quad.flags & world::QuadInward)) {
                emitGpu(index, bone, layer, lit, oneSided || !(quad.flags & world::QuadTwoSided), blend);
                return;
            }
            interpolatePose();
            emitQuad(quad, bone < matrices.size() ? &matrices[bone] : nullptr, layer, blend, oneSided, lit, uvAnim, 0.0f);
        };
        auto emitWorn = [&](const world::EntityRig& worn, const std::vector<int32_t>& wearer, const std::vector<uint8_t>& hidden, uint32_t layer, float offsetV, bool showHead) {
            interpolatePose();
            for (size_t index = 0; index < worn.quads.size(); ++index) {
                size_t piece = index < worn.quadBones.size() ? worn.quadBones[index] : wearer.size();
                int32_t bone = piece < wearer.size() ? wearer[piece] : -1;
                if (bone >= 0 && size_t(bone) < hidden.size() && hidden[size_t(bone)]) {
                    std::string name = lowercase(worn.bones[piece].name);
                    if (!showHead || (name != "head" && name != "hat")) {
                        continue;
                    }
                }
                const world::BoneMatrix* matrix = bone >= 0 && size_t(bone) < matrices.size() ? &matrices[size_t(bone)] : nullptr;
                emitQuad(worn.quads[index], matrix, layer, world::EntityBlend::Opaque, false, true, {}, offsetV);
            }
        };
        if (!invisible && combined) {
            for (size_t index = 0; index < model->controllers.size(); ++index) {
                const world::EntityRenderController& source = model->controllers[index];
                if (!source.condition.empty() && animator.evaluate(source.condition) == 0.0) {
                    continue;
                }
                uint32_t picked = pickChoice(animator, source.geometry, source.geometryChoices);
                uint32_t layer = textureOf(&source);
                const std::vector<uint8_t>& hidden = hiddenBones(source).get();
                std::array<uint32_t, 2> uvAnim = uvAnimOf(&source);
                setSurface(&source, layer);
                for (size_t quad = 0; quad < rig.quads.size(); ++quad) {
                    const world::CombinedQuadSource& from = model->combinedSources[quad];
                    if (from.controller == index && from.rig == picked) {
                        emit(quad, layer, hidden, renderingMaterial.blend, renderingMaterial.oneSided, !source.ignoreLighting, uvAnim);
                    }
                }
            }
        } else if (!invisible) {
            uint32_t layer = skinView && skinView->base.present ? blockAssets->skinLayerBase() + skinView->base.layer : textureOf(controller);
            actorPartHidden.clear();
            const std::vector<uint8_t>& hidden = controller ? hiddenBones(*controller).get() : actorPartHidden;
            bool lit = !controller || !controller->ignoreLighting;
            std::array<uint32_t, 2> uvAnim = uvAnimOf(controller);
            setSurface(controller, layer);
            for (size_t index = 0; index < rig.quads.size(); ++index) {
                emit(index, layer, hidden, renderingMaterial.blend, renderingMaterial.oneSided, lit, uvAnim);
            }
            if (skinView) {
                double lifeTime = animator.evaluate(lifeTimeScript());
                double blinking = animator.evaluate(blinkingScript());
                for (const SkinAnimationView& animation : skinView->animations) {
                    float offsetV = skinFrameOffset(animation.kind, animation.blinking, animation.frames, std::isfinite(lifeTime) ? lifeTime : 0.0, std::isfinite(blinking) ? blinking : 0.0);
                    emitWorn(*animation.rig, wornBones(*animation.rig, rig), hidden, blockAssets->skinLayerBase() + animation.texture.layer, offsetV, true);
                }
                const world::EntityRig* cape = blockAssets->capeRig();
                if (cape && skinView->cape.present && actor.armor[1] != "minecraft:elytra") {
                    emitWorn(*cape, wornBones(*cape, rig), hidden, blockAssets->skinLayerBase() + skinView->cape.layer, 0.0f, false);
                }
            }
            const world::EntityModel* elytra = actor.armor[1] == "minecraft:elytra" ? blockAssets->attachableModel("minecraft:elytra") : nullptr;
            int32_t body = -1;
            for (size_t bone = 0; elytra && bone < rig.bones.size(); ++bone) {
                if (lowercase(rig.bones[bone].name) == "body") {
                    body = static_cast<int32_t>(bone);
                }
            }
            if (elytra && !elytra->rigs.empty() && body >= 0) {
                const world::EntityRig& wings = elytra->rigs.front();
                HeldAttachable& worn = actorElytras[actor.runtimeId];
                worn.identifier = "minecraft:elytra";
                worn.bones = wings.bones;
                worn.animator.update(elytra->scripts.get(), &blockAssets->animationLibrary(), worn.bones, input);
                const std::vector<world::BoneMatrix>& wingMatrices = worn.animator.matrices();
                interpolatePose();
                if (wingMatrices.size() == wings.bones.size() && size_t(body) < matrices.size()) {
                    // A player with a cape wears it on the elytra, as the game does.
                    uint32_t layer = skinView && skinView->cape.present ? blockAssets->skinLayerBase() + skinView->cape.layer : elytra->layer;
                    for (size_t index = 0; index < wings.quads.size(); ++index) {
                        size_t bone = index < wings.quadBones.size() ? wings.quadBones[index] : wings.bones.size();
                        if (bone >= wingMatrices.size()) {
                            continue;
                        }
                        world::BoneMatrix placed = compose(matrices[size_t(body)], wingMatrices[bone]);
                        size_t first = out.size();
                        emitQuad(wings.quads[index], &placed, layer, world::EntityBlend::Opaque, false, true, {}, 0.0f);
                        const HudItem& chest = actor.runtimeId == LocalActorId ? hudState.armor[1] : actor.armorItems[1];
                        if (chest.enchanted) for (size_t q = first; q < out.size(); ++q) {
                            world::applyItemGlint(out[q], blockAssets->armorGlintLayer(), now, visuals.glintStrength.value_or(menu.glintStrength()), visuals.glintSpeed.value_or(menu.glintSpeed()));
                        }
                    }
                }
            }
        }
        size_t firstWorn = out.size();
        auto toWorld = [&](const std::array<float, 3>& posed) {
            std::array<float, 3> p { posed[0] * scale, posed[1] * scale, posed[2] * scale };
            tilt(p[0], p[1], p[2]);
            return std::array<float, 3> { baseX + cosine * p[0] + sine * p[2], baseY + p[1], baseZ - sine * p[0] + cosine * p[2] };
        };
        if (std::any_of(actor.armor.begin(), actor.armor.end(), [](const std::string& item) { return !item.empty(); })) {
            interpolatePose();
            bool hurt = actor.lastHurt > 0.0 && now - actor.lastHurt < 0.5;
            appendArmor(actor.armor, rig, matrices, toWorld, hurt ? 1u << 7 : 0u, out, nullptr, actor.runtimeId == LocalActorId ? &hudState.armor : &actor.armorItems);
        }
        if (actor.runtimeId == LocalActorId) {
            interpolatePose();
            appendThirdPersonItem(hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))], input.itemUseTicks, bodyAttachable, rig, matrices, toWorld, out);
        } else if (!actor.held.empty()) {
            interpolatePose();
            appendThirdPersonItem(actor.held, input.itemUseTicks, actorAttachables[actor.runtimeId], rig, matrices, toWorld, out);
        }
        const HudItem& offhand = actor.runtimeId == LocalActorId ? hudState.offhand : actor.offhand;
        if (!offhand.empty()) {
            interpolatePose();
            HeldAttachable& state = actor.runtimeId == LocalActorId ? bodyOffhandAttachable : actorOffhandAttachables[actor.runtimeId];
            appendThirdPersonItem(offhand, 0.0, state, rig, matrices, toWorld, out, true);
        }
        lightQuads(out, firstWorn, light);
    }
    appendFrameItems(origin, out);
    appendShelfItems(origin, out);
    appendVaultItems(origin, out);
    appendPots(origin, out);
    appendBeaconBeams(origin, out, blended);
    appendPistons(origin, out, blended);
    for (auto it = animators.begin(); it != animators.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = animators.erase(it);
        }
    }
    std::erase_if(actorGeometry, [&](const auto& entry) { return heldItemFrame - entry.second.used > 120; });
    for (auto it = actorPoses.begin(); it != actorPoses.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = actorPoses.erase(it);
        }
    }
    for (auto it = swimAmounts.begin(); it != swimAmounts.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = swimAmounts.erase(it);
        }
    }
    for (auto it = actorAttachables.begin(); it != actorAttachables.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = actorAttachables.erase(it);
        }
    }
    std::erase_if(actorOffhandAttachables, [&](const auto& entry) { return !present.count(entry.first); });
    std::erase_if(actorElytras, [&](const auto& entry) { return !present.count(entry.first); });
    for (auto it = actorItemUseSince.begin(); it != actorItemUseSince.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = actorItemUseSince.erase(it);
        }
    }
    lastActorTime = now;
    actorViews.resize(actualActors);
}

/**
 * The armor a humanoid wears, drawn with the vanilla armor models. Their bones
 * share the humanoid's names, so each armor bone follows the pose of the
 * wearer's bone of the same name, whatever geometry the skin brings.
 */
void Client::appendArmor(const std::array<std::string, 4>& armor, const world::EntityRig& rig, const std::vector<world::BoneMatrix>& matrices, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, uint32_t shadeFlags, std::vector<world::ModelQuadGpu>& out, const std::vector<uint8_t>* shownBones, const std::array<HudItem, 4>* items)
{
    for (size_t slot = 0; slot < armor.size(); ++slot) {
        if (armor[slot].empty()) {
            continue;
        }
        world::ArmorLook look = blockAssets->armorLook(slot, armor[slot]);
        if (!look.rig) {
            continue;
        }
        std::vector<int32_t>& wearer = armorBoneMatches[{ look.rig, &rig }];
        if (wearer.size() != look.rig->bones.size()) {
            wearer.assign(look.rig->bones.size(), -1);
            for (size_t piece = 0; piece < look.rig->bones.size(); ++piece) {
                std::string name = look.rig->bones[piece].name;
                for (size_t bone = 0; bone < rig.bones.size(); ++bone) {
                    if (matchesPattern(lowercase(name), rig.bones[bone].name)) {
                        wearer[piece] = static_cast<int32_t>(bone);
                        break;
                    }
                }
            }
        }
        world::EntityTileGrid grid = blockAssets->entityTileGrid(look.layer);
        for (size_t index = 0; index < look.rig->quads.size(); ++index) {
            size_t piece = index < look.rig->quadBones.size() ? look.rig->quadBones[index] : wearer.size();
            int32_t bone = piece < wearer.size() ? wearer[piece] : -1;
            if (bone < 0 || size_t(bone) >= matrices.size() || (shownBones && !(*shownBones)[size_t(bone)])) {
                continue;
            }
            const world::BoneMatrix& m = matrices[size_t(bone)];
            const world::ModelQuad& quad = look.rig->quads[index];
            auto place = [&](const std::array<float, 3>& point) {
                return toWorld({
                    m[0] * point[0] + m[1] * point[1] + m[2] * point[2] + m[3],
                    m[4] * point[0] + m[5] * point[1] + m[6] * point[2] + m[7],
                    m[8] * point[0] + m[9] * point[1] + m[10] * point[2] + m[11],
                });
            };
            std::array<QuadCorner, 4> corners;
            std::array<float, 3> center {};
            for (size_t corner = 0; corner < 4; ++corner) {
                std::array<float, 3> point { quad.positions[corner][0] / 16.0f, quad.positions[corner][1] / 16.0f, quad.positions[corner][2] / 16.0f };
                for (size_t axis = 0; axis < 3; ++axis) {
                    center[axis] += point[axis] * 0.25f;
                }
                corners[corner].position = place(point);
                corners[corner].uv = { quad.uvs[corner][0] / 4096.0f, quad.uvs[corner][1] / 4096.0f };
            }
            size_t first = out.size();
            appendTiled(corners, look.layer, grid, world::posedShadeFace(quad.flags & world::QuadFaceMask, center, 1.0f, place) | EntityQuadFlag | shadeFlags, out);
            if (armor[slot].starts_with("minecraft:leather_")) {
                constexpr uint32_t defaultColor = (uint32_t(ui::LeatherColor[0]) << 16) | (uint32_t(ui::LeatherColor[1]) << 8) | ui::LeatherColor[2];
                uint32_t tint = 0xa0000000u | ((items ? (*items)[slot].customColor.value_or(defaultColor) : defaultColor) & 0xffffffu);
                for (size_t q = first; q < out.size(); ++q) out[q].words[14] = tint;
            }
            if (items && (*items)[slot].enchanted) {
                for (size_t q = first; q < out.size(); ++q) {
                    world::applyItemGlint(out[q], blockAssets->armorGlintLayer(), secondsNow(), visuals.glintStrength.value_or(menu.glintStrength()), visuals.glintSpeed.value_or(menu.glintSpeed()));
                }
            }
        }
    }
}

/**
 * How long the local player has been using the selected item, in ticks with
 * the fraction of the current one.
 */
double Client::localItemUseTicks() const
{
    // A use that just started still counts as under way.
    return hudState.itemUseStarted > 0.0 ? std::max((secondsNow() - hudState.itemUseStarted) * TicksPerSecond, 1.0e-3) : 0.0;
}

/**
 * How long an entity has been using its held item. Other players only come
 * with the using item flag, so their use is timed from when it came on.
 */
double Client::actorItemUseTicks(const ActorView& actor, double now)
{
    if (actor.runtimeId == LocalActorId) {
        return localItemUseTicks();
    }
    if (!(actor.flags[0] & UsingItemFlag)) {
        actorItemUseSince.erase(actor.runtimeId);
        return 0.0;
    }
    double since = actorItemUseSince.try_emplace(actor.runtimeId, now).first->second;
    return std::max((now - since) * TicksPerSecond, 1.0e-3);
}

/**
 * The held item's attachable from a server pack, drawn on the holder the way
 * the game binds it: the bone with a binding hangs from the holder's right
 * item bone instead of its own parents, and the bones under it follow. An
 * attachable without a binding hangs from that bone as a whole. Returns false
 * when the item has no attachable, so the caller draws it as usual.
 */
bool Client::appendAttachable(const HudItem& held, double itemUseTicks, const world::EntityRig& holder, const std::vector<world::BoneMatrix>& holderMatrices, bool firstPerson, HeldAttachable& state, const std::function<std::array<float, 3>(const std::array<float, 3>&)>& toWorld, std::vector<world::ModelQuadGpu>& out, bool offhand)
{
    const world::EntityModel* model = held.empty() || !blockAssets ? nullptr : blockAssets->attachableModel(held.identifier);
    if (!model || model->wearable || model->rigs.empty()) {
        return false;
    }
    int32_t itemBone = -1;
    for (size_t bone = 0; bone < holder.bones.size() && bone < holderMatrices.size(); ++bone) {
        if (lowercase(holder.bones[bone].name) == (offhand ? "leftitem" : "rightitem")) {
            itemBone = static_cast<int32_t>(bone);
        }
    }
    if (itemBone < 0) {
        return false;
    }
    if (state.identifier != held.identifier) {
        state = HeldAttachable {};
        state.identifier = held.identifier;
    }
    state.bones = model->rigs.front().bones;
    const std::array<float, 3>& holderPivot = holder.bones[size_t(itemBone)].pivot;
    for (world::EntityBone& bone : state.bones) {
        if (bone.anchoredToHolder) {
            bone.pivot = holderPivot;
        }
    }
    world::AnimationInput input;
    input.now = secondsNow();
    input.worldTime = currentWorldTime(timeState);
    input.identifier = held.identifier;
    input.mainHandItem = held.identifier;
    input.itemUseTicks = itemUseTicks;
    input.contextVariables = { { "is_first_person", firstPerson ? 1.0 : 0.0 }, { "item_slot", offhand ? 1.0 : 0.0 } };
    state.animator.update(model->scripts.get(), &blockAssets->animationLibrary(), state.bones, input);
    const std::vector<world::BoneMatrix>& matrices = state.animator.matrices();
    if (matrices.size() != state.bones.size()) {
        return true;
    }
    // The render controller picks the frame, like the bow's pull stages, by geometry and texture.
    const world::EntityRig* chosenRig = &model->rigs.front();
    uint32_t layer = model->layer;
    for (const world::EntityRenderController& controller : model->controllers) {
        if (!controller.condition.empty() && state.animator.evaluate(controller.condition) == 0.0) {
            continue;
        }
        uint32_t rigIndex = pickChoice(state.animator, controller.geometry, controller.geometryChoices);
        if (rigIndex < model->rigs.size() && model->rigs[rigIndex].bones.size() == state.bones.size()) {
            chosenRig = &model->rigs[rigIndex];
        }
        if (uint32_t chosen = pickChoice(state.animator, controller.texture, controller.textureChoices); chosen != world::NoEntityChoice) {
            layer = chosen;
        }
        break;
    }
    const world::EntityRig& rig = *chosenRig;

    int32_t bound = -1;
    for (size_t bone = 0; bone < rig.bones.size() && bound < 0; ++bone) {
        if (rig.bones[bone].bound) {
            bound = static_cast<int32_t>(bone);
        }
    }
    world::BoneMatrix detach { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    if (bound >= 0 && rig.bones[size_t(bound)].parent >= 0) {
        std::optional<world::BoneMatrix> inverse = invert(matrices[size_t(rig.bones[size_t(bound)].parent)]);
        if (!inverse) {
            return true;
        }
        detach = *inverse;
    }
    world::BoneMatrix hand = holderMatrices[size_t(itemBone)];
    if (bound >= 0) {
        hand = boundAttachableFrame(hand, holderPivot);
    }
    hand = compose(hand, detach);
    std::vector<uint8_t> attached(rig.bones.size(), bound < 0 ? 1 : 0);
    for (size_t bone = 0; bone < rig.bones.size() && bound >= 0; ++bone) {
        for (int32_t walker = static_cast<int32_t>(bone), steps = 0; walker >= 0 && steps <= int32_t(rig.bones.size()); walker = rig.bones[size_t(walker)].parent, ++steps) {
            if (walker == bound) {
                attached[bone] = 1;
                break;
            }
        }
    }
    world::EntityTileGrid grid = blockAssets->entityTileGrid(layer);
    for (size_t index = 0; index < rig.quads.size(); ++index) {
        size_t bone = index < rig.quadBones.size() ? rig.quadBones[index] : rig.bones.size();
        if (bone >= rig.bones.size() || !attached[bone]) {
            continue;
        }
        world::BoneMatrix m = compose(hand, matrices[bone]);
        const world::ModelQuad& quad = rig.quads[index];
        std::array<float, 3> anchor = state.bones[bone].anchoredToHolder ? holderPivot : std::array<float, 3> {};
        auto place = [&](const std::array<float, 3>& point) {
            return toWorld({
                m[0] * point[0] + m[1] * point[1] + m[2] * point[2] + m[3],
                m[4] * point[0] + m[5] * point[1] + m[6] * point[2] + m[7],
                m[8] * point[0] + m[9] * point[1] + m[10] * point[2] + m[11],
            });
        };
        std::array<QuadCorner, 4> corners;
        std::array<float, 3> center {};
        for (size_t corner = 0; corner < 4; ++corner) {
            std::array<float, 3> point { quad.positions[corner][0] / 16.0f + anchor[0], quad.positions[corner][1] / 16.0f + anchor[1], quad.positions[corner][2] / 16.0f + anchor[2] };
            for (size_t axis = 0; axis < 3; ++axis) {
                center[axis] += point[axis] * 0.25f;
            }
            corners[corner].position = place(point);
            corners[corner].uv = { quad.uvs[corner][0] / 4096.0f, quad.uvs[corner][1] / 4096.0f };
        }
        appendTiled(corners, layer, grid, world::posedShadeFace(quad.flags & world::QuadFaceMask, center, 1.0f, place) | EntityQuadFlag, out);
    }
    return true;
}

/**
 * Replaces every entity's network position and rotation with its smoothed
 * one. A new sample starts a glide from the displayed state toward it, lasting
 * as long as the gap since the previous sample (one to three ticks); rotations
 * take the shortest way round. New entities, teleports and jumps longer than
 * eight blocks snap.
 */
/**
 * Names over entities the way the game floats them: every visible player,
 * anything flagged to always show its name, which is how servers put
 * floating text in the world, and show-name mobs near the crosshair, within
 * each entity's nameplate distance. The score tag joins within ten blocks.
 * A tag hangs 0.7 blocks over the entity's box, 0.125 higher per extra line,
 * at 1.6/60 blocks per font pixel on a camera-facing plane; see-through tags
 * come first, then a sneaking entity's depth tested one, each back to front.
 */
std::vector<menu::NameTag> Client::buildNameTags() const
{
    std::vector<std::pair<double, menu::NameTag>> placed;
    float scale = guiScale();
    float width = static_cast<float>(window->width());
    float height = static_cast<float>(window->height());
    Mat4 matrix = camera.viewProjection(width / std::max(height, 1.0f));
    float focalPixels = height / (2.0f * camera.halfVerticalTangent(width / std::max(height, 1.0f)));
    float tagScale = visuals.nametagScale.value_or(1.0f);
    std::vector<const ActorView*> named;
    named.reserve(actorViews.size() + 1);
    for (const ActorView& actor : actorViews) {
        named.push_back(&actor);
    }
    // your own tag, the way other players see it, when a mod asks and the camera is off your head
    ActorView self;
    bool ownShown = visuals.ownNametag.value_or(false) && playerView.active && seenSessionSnapshot
        && (perspective != PerspectiveFirst || cameraDetached) && !seenSessionSnapshot->displayName.empty();
    if (ownShown) {
        self.runtimeId = LocalActorId;
        self.identifier = "minecraft:player";
        self.name = seenSessionSnapshot->displayName;
        self.x = eyePosition[0];
        self.y = eyePosition[1] - playerView.eyeHeight();
        self.z = eyePosition[2];
        self.flags = seenSessionSnapshot->localActorFlags;
        self.flags[0] = (self.flags[0] & ~SneakingFlag) | (playerView.sneaking ? SneakingFlag : 0);
        named.push_back(&self);
    }
    for (const ActorView* candidate : named) {
        const ActorView& actor = *candidate;
        if (actor.name.empty() || (actor.flags[0] & InvisibleFlag) != 0) {
            continue;
        }
        bool player = actor.identifier == "minecraft:player";
        bool sneaking = (actor.flags[0] & SneakingFlag) != 0;
        bool always = player || actor.alwaysShowName || (actor.flags[0] & AlwaysShowNameFlag) != 0;
        if (!always && (actor.flags[0] & CanShowNameFlag) == 0) {
            continue;
        }
        std::array<double, 3> feet { actor.x, actor.y, actor.z };
        if (auto motion = motions.find(actor.runtimeId); motion != motions.end()) {
            feet = motion->second.shown;
        }
        double fx = feet[0] - camera.x();
        double fy = feet[1] - camera.y();
        double fz = feet[2] - camera.z();
        double distance = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (!std::isfinite(distance) || distance > actor.nameplateDistance) {
            continue;
        }
        std::vector<std::string> lines;
        auto split = [&](const std::string& text) {
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!line.empty()) {
                    lines.push_back(std::move(line));
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
        };
        split(actor.name);
        if (distance < ScoreTagDistance) {
            split(actor.scoreTag);
        }
        if (lines.empty()) {
            continue;
        }
        double box = actorExtent(actor.height, sneaking ? SneakingHeight : StandingHeight, actor.scale);
        double lift = box + HeadClearance + ExtraLineRaise * static_cast<double>(lines.size() - 1);
        auto anchor = project(matrix, fx, fy + lift, fz);
        if (!anchor) {
            continue;
        }
        float pixelX = static_cast<float>(((*anchor)[0] + 1.0) * 0.5 * width);
        float pixelY = static_cast<float>((1.0 - (*anchor)[1]) * 0.5 * height);
        if (!always && std::hypot(pixelX - width * 0.5f, pixelY - height * 0.5f) > CrosshairRadius) {
            continue;
        }
        menu::NameTag tag;
        for (size_t line = 0; line < lines.size(); ++line) {
            tag.text += (line ? "\n" : "") + lines[line];
        }
        tag.x = pixelX / scale;
        tag.y = pixelY / scale;
        tag.magnify = NameTagPixelSize * focalPixels / (static_cast<float>((*anchor)[2]) * scale) * tagScale;
        tag.depth = static_cast<float>((*anchor)[3]);
        tag.sneaking = sneaking;
        placed.emplace_back(distance, std::move(tag));
    }
    std::sort(placed.begin(), placed.end(), [](const auto& a, const auto& b) {
        if (a.second.sneaking != b.second.sneaking) {
            return !a.second.sneaking;
        }
        return a.first > b.first;
    });
    std::vector<menu::NameTag> tags;
    tags.reserve(placed.size());
    for (auto& [distance, tag] : placed) {
        tags.push_back(std::move(tag));
    }
    return tags;
}

/**
 * The local player as the third person views draw it, its body trailing the
 * head like every other player's.
 */
ActorView Client::localActorView(float deltaSeconds)
{
    bool inWater = seenSessionSnapshot && session.cameraEnvironment(*seenSessionSnapshot,
        { eyePosition[0], eyePosition[1] - playerView.eyeHeight() + 0.1, eyePosition[2] }, false).first == 1;
    bool swimming = playerView.sprinting && inWater;
    localSwimAmount = std::clamp(localSwimAmount + (swimming ? 1.0f : -1.0f)
        * std::clamp(deltaSeconds, 0.0f, 0.25f) * 4.0f, 0.0f, 1.0f);
    ActorView self;
    self.runtimeId = LocalActorId;
    self.lastHurt = hudState.lastHurt;
    self.identifier = "minecraft:player";
    self.x = eyePosition[0];
    self.y = eyePosition[1] - playerView.eyeHeight();
    self.z = eyePosition[2];
    self.headYaw = localLookYaw;
    self.pitch = localLookPitch;
    if (seenSessionSnapshot) self.flags = seenSessionSnapshot->localActorFlags;
    self.flags[0] &= ~((1ull << 1) | (1ull << 3) | (1ull << SwimmingFlag) | (1ull << GlidingFlag));
    self.flags[0] |= (playerView.sneaking ? 1ull << 1 : 0) | (playerView.sprinting ? 1ull << 3 : 0) | (swimming ? 1ull << SwimmingFlag : 0)
        | (playerView.gliding ? 1ull << GlidingFlag : 0);
    self.skinSlot = localSkinSlot;
    self.slim = localSlim;
    for (size_t slot = 0; slot < self.armor.size(); ++slot) {
        self.armor[slot] = hudState.armor[slot].empty() ? std::string() : hudState.armor[slot].identifier;
    }

    double dx = playerView.current[0] - playerView.previous[0];
    double dz = playerView.current[2] - playerView.previous[2];
    localBodyYaw = trailBody(localBodyYaw, self.headYaw, dx, dz, deltaSeconds * 20.0f);
    self.yaw = localBodyYaw;
    self.velocity = { float(dx), float(playerView.current[1] - playerView.previous[1]), float(dz) };
    if (!playerView.gliding) {
        localGlideSince = 0.0;
    } else if (localGlideSince == 0.0) {
        localGlideSince = secondsNow();
    }
    return self;
}

/**
 * The whole-body tilt of a gliding actor, in degrees: pitched toward lying
 * along the look direction, easing in over its first ten gliding ticks, and
 * turned toward its horizontal motion. Only the local player counts its
 * gliding ticks, so other players ease in by the frame fraction alone and
 * stay nearly upright, as in the game. The view vector keeps its horizontal
 * length, which the game does not normalise.
 */
std::optional<std::array<float, 2>> Client::glideRotation(const ActorView& actor, double now, float alpha) const
{
    if ((actor.flags[0] & (1ull << GlidingFlag)) == 0) {
        return std::nullopt;
    }
    bool local = actor.runtimeId == LocalActorId;
    if (local && perspective == PerspectiveFirst && !cameraDetached) {
        return std::nullopt;
    }
    float ticks = local && localGlideSince > 0.0 ? float((now - localGlideSince) * TicksPerSecond) + 1.0f : alpha;
    float ease = std::clamp(ticks * ticks / 100.0f, 0.0f, 1.0f);
    float pitch = (-90.0f - actor.pitch) * ease;

    constexpr float Radians = 0.017453292f;
    constexpr float Degrees = 57.295776f;
    constexpr float DeadZone = 0.0625f;
    float yawAngle = -3.14159265f - actor.headYaw * Radians;
    float horizontal = -motion::cosine(-(actor.pitch * Radians));
    float viewZ = motion::cosine(yawAngle) * horizontal;
    float viewX = horizontal * motion::sine(yawAngle);
    float deltaX = actor.velocity[0];
    float deltaZ = actor.velocity[2];
    float moving = deltaZ * deltaZ + deltaX * deltaX;
    float turn = 0.0f;
    if (viewZ * viewZ + viewX * viewX > 0.0f && moving > 0.0f) {
        float cross = viewZ * deltaX - viewX * deltaZ;
        float side = std::abs(cross) < DeadZone ? 0.0f : (cross > 0.0f ? 1.0f : -1.0f);
        float angle = std::acos((viewZ * deltaZ + viewX * deltaX) / std::sqrt(moving)) * side;
        turn = std::isfinite(angle) ? angle * Degrees : 0.0f;
    }
    return std::array<float, 2> { pitch, turn };
}

void Client::interpolateActors(double now)
{
    constexpr double SnapDistance = 8.0;
    auto& present = actorPresent;
    present.clear();
    if (present.bucket_count() * present.max_load_factor() < actorViews.size()) present.reserve(actorViews.size());
    for (ActorView& actor : actorViews) {
        present.insert(actor.runtimeId);
        if (world::projectileEntity(actor.identifier)) {
            if (actor.projectileTickTime > 0.0) {
                const double alpha = std::clamp((now - actor.projectileTickTime) * TicksPerSecond, 0.0, 1.0);
                actor.x = actor.projectilePrevious[0] + (actor.x - actor.projectilePrevious[0]) * alpha;
                actor.y = actor.projectilePrevious[1] + (actor.y - actor.projectilePrevious[1]) * alpha;
                actor.z = actor.projectilePrevious[2] + (actor.z - actor.projectilePrevious[2]) * alpha;
                actor.yaw = actor.projectilePreviousTurn[0] + wrapDegrees(actor.yaw - actor.projectilePreviousTurn[0]) * float(alpha);
                actor.headYaw = actor.projectilePreviousTurn[1] + wrapDegrees(actor.headYaw - actor.projectilePreviousTurn[1]) * float(alpha);
                actor.pitch = actor.projectilePreviousTurn[2] + wrapDegrees(actor.pitch - actor.projectilePreviousTurn[2]) * float(alpha);
            }
            continue;
        }
        std::array<double, 3> target { actor.x, actor.y, actor.z };
        std::array<float, 3> turn { actor.yaw, actor.headYaw, actor.pitch };
        auto [entry, created] = motions.try_emplace(actor.runtimeId);
        ActorMotion& motion = entry->second;
        double jump = std::sqrt((target[0] - motion.shown[0]) * (target[0] - motion.shown[0]) + (target[1] - motion.shown[1]) * (target[1] - motion.shown[1]) + (target[2] - motion.shown[2]) * (target[2] - motion.shown[2]));
        if (created || actor.teleports != motion.teleports || jump > SnapDistance) {
            motion.from = target;
            motion.to = target;
            motion.shown = target;
            motion.turnFrom = turn;
            motion.turnTo = turn;
            motion.turnShown = turn;
            motion.start = now;
            motion.duration = 0.0;
            motion.lastSample = now;
            motion.moves = actor.moves;
            motion.teleports = actor.teleports;
            motion.launchTurns = actor.launchTurns;
            motion.bodyYaw = actor.yaw;
            motion.lastFrame = now;
            motion.lastShown = target;
        } else if (actor.moves != motion.moves) {
            motion.retarget(target, turn, now);
            motion.moves = actor.moves;
        }
        if (actor.launchTurns != motion.launchTurns) {
            motion.launchTurns = actor.launchTurns;
            motion.turnFrom = turn;
            motion.turnShown = turn;
            if (now - motion.start >= motion.duration) {
                motion.turnTo = turn;
            }
        }
        motion.advance(now);
        actor.x = motion.shown[0];
        actor.y = motion.shown[1];
        actor.z = motion.shown[2];
        actor.yaw = motion.turnShown[0];
        actor.headYaw = motion.turnShown[1];
        actor.pitch = motion.turnShown[2];
        float ticks = static_cast<float>(std::min(now - motion.lastFrame, 0.25) * 20.0);
        if (actor.identifier == "minecraft:player") {
            if (ticks > 0.0f) {
                double stepX = (motion.shown[0] - motion.lastShown[0]) / ticks;
                double stepZ = (motion.shown[2] - motion.lastShown[2]) / ticks;
                motion.bodyYaw = trailBody(motion.bodyYaw, actor.headYaw, stepX, stepZ, ticks);
            }
            actor.yaw = motion.bodyYaw;
        }
        motion.lastShown = motion.shown;
        motion.lastFrame = now;
    }
    for (auto it = motions.begin(); it != motions.end();) {
        if (present.count(it->first)) {
            ++it;
        } else {
            it = motions.erase(it);
        }
    }
}

}
