#include "world/BlockCollisions.h"
#include "util/Text.h"

#include "BlockCollisionTable.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>

namespace kestrel::world {

namespace {

constexpr int FaceNorth = 2;
constexpr int FaceSouth = 3;
constexpr int FaceWest = 4;
constexpr int FaceEast = 5;

constexpr std::array<std::array<int32_t, 3>, 6> FaceOffsets { {
    { 0, -1, 0 },
    { 0, 1, 0 },
    { 0, 0, -1 },
    { 0, 0, 1 },
    { -1, 0, 0 },
    { 1, 0, 0 },
} };

constexpr std::string_view ThinConnectors[] = {
    "glass_pane",
    "black_stained_glass_pane",
    "blue_stained_glass_pane",
    "brown_stained_glass_pane",
    "cyan_stained_glass_pane",
    "gray_stained_glass_pane",
    "green_stained_glass_pane",
    "light_blue_stained_glass_pane",
    "light_gray_stained_glass_pane",
    "lime_stained_glass_pane",
    "magenta_stained_glass_pane",
    "orange_stained_glass_pane",
    "pink_stained_glass_pane",
    "purple_stained_glass_pane",
    "red_stained_glass_pane",
    "white_stained_glass_pane",
    "yellow_stained_glass_pane",
    "iron_bars",
    "copper_bars",
    "exposed_copper_bars",
    "weathered_copper_bars",
    "oxidized_copper_bars",
    "waxed_copper_bars",
    "waxed_exposed_copper_bars",
    "waxed_weathered_copper_bars",
    "waxed_oxidized_copper_bars",
    "cobblestone_wall",
    "cobbled_deepslate_wall",
};

int opposite(int face)
{
    return face ^ 1;
}

int rotateClockwise(int face)
{
    switch (face) {
    case FaceNorth:
        return FaceEast;
    case FaceEast:
        return FaceSouth;
    case FaceSouth:
        return FaceWest;
    default:
        return FaceNorth;
    }
}

int rotateCounterClockwise(int face)
{
    switch (face) {
    case FaceNorth:
        return FaceWest;
    case FaceWest:
        return FaceSouth;
    case FaceSouth:
        return FaceEast;
    default:
        return FaceNorth;
    }
}

int axisOf(int face)
{
    return face >> 1;
}

class LineReader {
public:
    explicit LineReader(std::string_view text)
        : text(text)
    {
    }

    bool next(std::string_view& line)
    {
        if (position >= text.size()) {
            return false;
        }
        size_t end = text.find('\n', position);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        line = text.substr(position, end - position);
        position = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        return true;
    }

private:
    std::string_view text;
    size_t position = 0;
};

class Fields {
public:
    explicit Fields(std::string_view line)
        : line(line)
    {
    }

    std::string_view word()
    {
        while (position < line.size() && line[position] == ' ') {
            ++position;
        }
        size_t start = position;
        while (position < line.size() && line[position] != ' ') {
            ++position;
        }
        return line.substr(start, position - start);
    }

    template <typename T>
    T number()
    {
        std::string_view text = word();
        T value {};
        if constexpr (std::is_floating_point_v<T>) {
            value = util::parseFloat(text);
        } else {
            std::from_chars(text.data(), text.data() + text.size(), value);
        }
        return value;
    }

private:
    std::string_view line;
    size_t position = 0;
};

}

const BlockCollisions& BlockCollisions::shared()
{
    static const BlockCollisions instance;
    return instance;
}

BlockCollisions::BlockCollisions()
{
    std::string_view text(reinterpret_cast<const char*>(KestrelBlockCollisionData::kBlockCollisions), KestrelBlockCollisionData::kBlockCollisionsSize);
    LineReader reader(text);
    std::string_view line;
    if (!reader.next(line)) {
        return;
    }
    Fields header(line);
    header.word();
    size_t nameCount = header.number<size_t>();
    names.reserve(nameCount);
    for (size_t index = 0; index < nameCount && reader.next(line); ++index) {
        names.emplace_back(line);
        if (line == "nether_brick_fence") {
            netherBrickFence = static_cast<uint16_t>(index);
        } else if (line == "glass") {
            glass = static_cast<uint16_t>(index);
        }
        bool thin = false;
        for (std::string_view connector : ThinConnectors) {
            thin = thin || line == connector;
        }
        thinNames.push_back(thin);
    }

    if (!reader.next(line)) {
        return;
    }
    Fields shapeHeader(line);
    shapeHeader.word();
    size_t shapeCount = shapeHeader.number<size_t>();
    shapes.reserve(shapeCount);
    for (size_t index = 0; index < shapeCount && reader.next(line); ++index) {
        Fields fields(line);
        size_t boxCount = fields.number<size_t>();
        std::vector<CollisionBox> shape(boxCount);
        for (CollisionBox& box : shape) {
            box.minX = fields.number<float>();
            box.minY = fields.number<float>();
            box.minZ = fields.number<float>();
            box.maxX = fields.number<float>();
            box.maxY = fields.number<float>();
            box.maxZ = fields.number<float>();
        }
        if (shape.size() == 1 && shape[0].minX == 0.0f && shape[0].minY == 0.0f && shape[0].minZ == 0.0f && shape[0].maxX == 1.0f && shape[0].maxY == 1.0f && shape[0].maxZ == 1.0f) {
            cube.shape = static_cast<int16_t>(index);
            cube.flags = CollisionSolid;
            cube.name = 0xFFFF;
        }        shapes.push_back(std::move(shape));
    }

    if (!reader.next(line)) {
        return;
    }
    Fields stateHeader(line);
    stateHeader.word();
    size_t stateCount = stateHeader.number<size_t>();
    states.reserve(stateCount);
    for (size_t index = 0; index < stateCount && reader.next(line); ++index) {
        Fields fields(line);
        uint32_t hash = fields.number<uint32_t>();
        CollisionState state;
        state.name = fields.number<uint16_t>();
        state.shape = fields.number<int16_t>();
        state.flags = fields.number<uint16_t>();
        state.face = static_cast<int8_t>(fields.number<int>());
        if (state.name < names.size() && names[state.name] == "air") {
            continue;
        }
        states.emplace(hash, state);
    }
}

const CollisionState* BlockCollisions::find(uint32_t hash) const
{
    auto found = states.find(hash);
    return found == states.end() ? nullptr : &found->second;
}

const std::string& BlockCollisions::name(const CollisionState& state) const
{
    static const std::string empty;
    return state.name < names.size() ? names[state.name] : empty;
}

std::vector<const CollisionState*> BlockCollisions::statesNamed(std::string_view value) const
{
    std::vector<const CollisionState*> found;
    for (const auto& [hash, state] : states) {
        if (name(state) == value) {
            found.push_back(&state);
        }
    }
    return found;
}

bool BlockCollisions::named(const CollisionState* state, std::string_view value) const
{
    return state && name(*state) == value;
}

bool BlockCollisions::fenceConnects(const CollisionState& self, const CollisionState* other, int face) const
{
    if (!other) {
        return false;
    }
    if (other->flags & CollisionFence) {
        if (other->name == netherBrickFence || self.name == netherBrickFence) {
            return other->name == self.name;
        }
        return true;
    }
    if (other->flags & CollisionTrapdoor) {
        return (other->flags & CollisionOpen) && other->face == face;
    }
    return (other->flags & CollisionFenceGate) || ((other->flags & CollisionSolid) && !(other->flags & CollisionTransparent));
}

bool BlockCollisions::wallConnects(const CollisionState* other, int face) const
{
    if (!other) {
        return false;
    }
    const std::string& otherName = name(*other);
    if (otherName == "glass_pane" || otherName == "iron_bars" || other->name == glass) {
        return true;
    }
    if (other->flags & (CollisionStainedGlass | CollisionWall)) {
        return true;
    }
    if (other->flags & CollisionFenceGate) {
        return other->face >= 0 && axisOf(other->face) != axisOf(face);
    }
    if (other->flags & CollisionStairs) {
        return other->face >= 0 && opposite(other->face) == face;
    }
    if (other->flags & CollisionTrapdoor) {
        return (other->flags & CollisionOpen) && other->face == face;
    }
    return (other->flags & CollisionSolid) && !(other->flags & CollisionTransparent);
}

bool BlockCollisions::thinConnects(const CollisionState* other, int face) const
{
    if (!other) {
        return false;
    }
    if (other->name < thinNames.size() && thinNames[other->name]) {
        return true;
    }
    if (other->flags & CollisionTrapdoor) {
        return (other->flags & CollisionOpen) && other->face == face;
    }
    return (other->flags & CollisionSolid) != 0;
}

void BlockCollisions::boxes(const CollisionState& state, int32_t x, int32_t y, int32_t z, const Lookup& lookup, std::vector<CollisionBox>& out) const
{
    auto neighbour = [&](int face) {
        return lookup(x + FaceOffsets[face][0], y + FaceOffsets[face][1], z + FaceOffsets[face][2]);
    };
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    float fz = static_cast<float>(z);
    auto push = [&](double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
        out.push_back({ static_cast<float>(x + minX), static_cast<float>(y + minY), static_cast<float>(z + minZ),
            static_cast<float>(x + maxX), static_cast<float>(y + maxY), static_cast<float>(z + maxZ) });
    };

    switch (state.shape) {
    case ShapeCustom:
        for (uint16_t i = 0; state.box && i < state.boxCount; ++i) {
            const CollisionBox& box = state.box[i];
            out.push_back({ fx + box.minX, fy + box.minY, fz + box.minZ, fx + box.maxX, fy + box.maxY, fz + box.maxZ });
        }
        return;
    case ShapeFence: {
        bool north = fenceConnects(state, neighbour(FaceNorth), FaceNorth);
        bool south = fenceConnects(state, neighbour(FaceSouth), FaceSouth);
        bool west = fenceConnects(state, neighbour(FaceWest), FaceWest);
        bool east = fenceConnects(state, neighbour(FaceEast), FaceEast);
        push(west ? 0.0 : 0.375, 0.0, north ? 0.0 : 0.375, east ? 1.0 : 0.625, 1.5, south ? 1.0 : 0.625);
        return;
    }
    case ShapeWall: {
        bool north = wallConnects(neighbour(FaceNorth), FaceNorth);
        bool south = wallConnects(neighbour(FaceSouth), FaceSouth);
        bool west = wallConnects(neighbour(FaceWest), FaceWest);
        bool east = wallConnects(neighbour(FaceEast), FaceEast);
        double n = north ? 0.0 : 0.25;
        double s = south ? 1.0 : 0.75;
        double w = west ? 0.0 : 0.25;
        double e = east ? 1.0 : 0.75;
        if (north && south && !west && !east) {
            w = 0.3125;
            e = 0.6875;
        } else if (!north && !south && west && east) {
            n = 0.3125;
            s = 0.6875;
        }
        push(w, 0.0, n, e, 1.5, s);
        return;
    }
    case ShapeThin: {
        bool north = thinConnects(neighbour(FaceNorth), FaceNorth);
        bool south = thinConnects(neighbour(FaceSouth), FaceSouth);
        bool west = thinConnects(neighbour(FaceWest), FaceWest);
        bool east = thinConnects(neighbour(FaceEast), FaceEast);
        push(west ? 0.0 : 7.0 / 16.0, 0.0, north ? 0.0 : 7.0 / 16.0, east ? 1.0 : 9.0 / 16.0, 1.0, south ? 1.0 : 9.0 / 16.0);
        return;
    }
    case ShapeVine: {
        int bits = state.face < 0 ? 0 : state.face;
        double f1 = 1.0;
        double f2 = 1.0;
        double f3 = 1.0;
        double f4 = 0.0;
        double f5 = 0.0;
        double f6 = 0.0;
        bool flag = bits > 0;
        if (bits & 0x02) {
            f4 = std::max(f4, 0.0625);
            f1 = 0.0;
            f2 = 0.0;
            f5 = 1.0;
            f3 = 0.0;
            f6 = 1.0;
            flag = true;
        }
        if (bits & 0x08) {
            f1 = std::min(f1, 0.9375);
            f4 = 1.0;
            f2 = 0.0;
            f5 = 1.0;
            f3 = 0.0;
            f6 = 1.0;
            flag = true;
        }
        if (bits & 0x01) {
            f3 = std::min(f3, 0.9375);
            f6 = 1.0;
            f1 = 0.0;
            f4 = 1.0;
            f2 = 0.0;
            f5 = 1.0;
            flag = true;
        }
        const CollisionState* above = neighbour(1);
        if (!flag && above && (above->flags & CollisionSolid)) {
            f2 = std::min(f2, 0.9375);
            f5 = 1.0;
            f1 = 0.0;
            f4 = 1.0;
            f3 = 0.0;
            f6 = 1.0;
        }
        push(f1, f2, f3, f4, f5, f6);
        return;
    }
    default:
        break;
    }

    if (state.shape < 0 || static_cast<size_t>(state.shape) >= shapes.size()) {
        return;
    }
    if ((state.flags & CollisionStairs) && state.face >= FaceNorth) {
        stairBoxes(state, x, y, z, lookup, out);
        return;
    }
    for (const CollisionBox& box : shapes[static_cast<size_t>(state.shape)]) {
        out.push_back({ fx + box.minX, fy + box.minY, fz + box.minZ, fx + box.maxX, fy + box.maxY, fz + box.maxZ });
    }
}

void BlockCollisions::stairBoxes(const CollisionState& state, int32_t x, int32_t y, int32_t z, const Lookup& lookup, std::vector<CollisionBox>& out) const
{
    int facing = state.face;
    bool upsideDown = (state.flags & CollisionUpsideDown) != 0;
    auto stairAt = [&](int face) -> const CollisionState* {
        const CollisionState* other = lookup(x + FaceOffsets[face][0], y + FaceOffsets[face][1], z + FaceOffsets[face][2]);
        return other && (other->flags & CollisionStairs) && other->face >= FaceNorth ? other : nullptr;
    };
    auto sameHalf = [&](const CollisionState* other) {
        return ((other->flags & CollisionUpsideDown) != 0) == upsideDown;
    };
    auto canTakeShape = [&](int side) {
        const CollisionState* other = stairAt(side);
        return !other || other->face != facing || !sameHalf(other);
    };

    enum class Shape {
        Straight,
        InnerLeft,
        InnerRight,
        OuterLeft,
        OuterRight,
    };
    Shape shape = Shape::Straight;
    const CollisionState* front = stairAt(facing);
    const CollisionState* back = stairAt(opposite(facing));
    if (front && sameHalf(front) && axisOf(front->face) != axisOf(facing) && canTakeShape(opposite(front->face))) {
        shape = front->face == rotateCounterClockwise(facing) ? Shape::OuterLeft : Shape::OuterRight;
    } else if (back && sameHalf(back) && axisOf(back->face) != axisOf(facing) && canTakeShape(back->face)) {
        shape = back->face == rotateCounterClockwise(facing) ? Shape::InnerLeft : Shape::InnerRight;
    }

    double slabMinY = upsideDown ? 0.5 : 0.0;
    double stepMinY = upsideDown ? 0.0 : 0.5;
    double stepMaxY = stepMinY + 0.5;
    auto half = [&](int face) {
        double minX = x;
        double maxX = x + 1.0;
        double minZ = z;
        double maxZ = z + 1.0;
        switch (face) {
        case FaceNorth:
            maxZ = z + 0.5;
            break;
        case FaceSouth:
            minZ = z + 0.5;
            break;
        case FaceWest:
            maxX = x + 0.5;
            break;
        case FaceEast:
            minX = x + 0.5;
            break;
        default:
            break;
        }
        return std::array<double, 4> { minX, minZ, maxX, maxZ };
    };
    auto pushHalf = [&](int face) {
        std::array<double, 4> area = half(face);
        out.push_back({ static_cast<float>(area[0]), static_cast<float>(y + stepMinY), static_cast<float>(area[1]),
            static_cast<float>(area[2]), static_cast<float>(y + stepMaxY), static_cast<float>(area[3]) });
    };
    auto pushQuarter = [&](int first, int second) {
        std::array<double, 4> a = half(first);
        std::array<double, 4> b = half(second);
        out.push_back({ static_cast<float>(std::max(a[0], b[0])), static_cast<float>(y + stepMinY), static_cast<float>(std::max(a[1], b[1])),
            static_cast<float>(std::min(a[2], b[2])), static_cast<float>(y + stepMaxY), static_cast<float>(std::min(a[3], b[3])) });
    };

    out.push_back({ static_cast<float>(x), static_cast<float>(y + slabMinY), static_cast<float>(z),
        static_cast<float>(x + 1.0), static_cast<float>(y + slabMinY + 0.5), static_cast<float>(z + 1.0) });
    switch (shape) {
    case Shape::Straight:
        pushHalf(facing);
        break;
    case Shape::OuterLeft:
        pushQuarter(facing, rotateCounterClockwise(facing));
        break;
    case Shape::OuterRight:
        pushQuarter(facing, rotateClockwise(facing));
        break;
    case Shape::InnerLeft:
        pushHalf(facing);
        pushQuarter(opposite(facing), rotateCounterClockwise(facing));
        break;
    case Shape::InnerRight:
        pushHalf(facing);
        pushQuarter(opposite(facing), rotateClockwise(facing));
        break;
    }
}

}
