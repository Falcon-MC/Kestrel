#include "world/Mesher.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <deque>

namespace kestrel::world {

namespace {

bool hiddenValue(const IdMapping& ids, uint32_t value)
{
    return ids.hidden && ids.hidden->count(value) != 0;
}

constexpr uint32_t Side = 16;
constexpr uint32_t FullColumn = (1u << Side) - 1u;
constexpr Face AllFaces[] = { Face::NegativeX, Face::PositiveX, Face::NegativeY, Face::PositiveY, Face::NegativeZ, Face::PositiveZ };

using Columns = std::array<std::array<uint32_t, Side>, Side>;

bool isNegative(Face face)
{
    return face == Face::NegativeX || face == Face::NegativeY || face == Face::NegativeZ;
}

std::array<uint32_t, 3> blockCoordinate(Face face, uint32_t slice, uint32_t u, uint32_t v)
{
    switch (face) {
    case Face::NegativeX:
    case Face::PositiveX:
        return { slice, v, u };
    case Face::NegativeY:
    case Face::PositiveY:
        return { u, slice, v };
    case Face::NegativeZ:
    case Face::PositiveZ:
        break;
    }
    return { u, v, slice };
}

std::array<uint32_t, 3> neighbourBoundaryCoordinate(Face face, uint32_t u, uint32_t v)
{
    switch (face) {
    case Face::NegativeX:
        return { Side - 1, v, u };
    case Face::PositiveX:
        return { 0, v, u };
    case Face::NegativeY:
        return { u, Side - 1, v };
    case Face::PositiveY:
        return { u, 0, v };
    case Face::NegativeZ:
        return { u, v, Side - 1 };
    case Face::PositiveZ:
        break;
    }
    return { u, v, 0 };
}

class PaletteFacts {
public:
    PaletteFacts(const BlockAssets& assets, const IdMapping& ids, const SubChunk* subChunk)
    {
        static const BlockVisual air { FlagAir, {} };
        if (!subChunk || subChunk->storages().empty()) {
            uniform = &air;
            return;
        }
        storage = &subChunk->storages().front();
        for (uint32_t value : storage->palette()) {
            resolved.push_back(hiddenValue(ids, value) ? &air : &assets.visual(value, ids.hashed, ids.sequential.get()));
        }
        if (storage->isUniform()) {
            uniform = resolved.front();
        }
    }

    const BlockVisual& at(uint32_t x, uint32_t y, uint32_t z) const
    {
        if (uniform) {
            return *uniform;
        }
        return *resolved[storage->paletteIndex(linearIndex(x, y, z))];
    }

    bool isAir() const
    {
        return uniform && (uniform->flags & FlagAir);
    }

    const BlockVisual* uniformVisual() const
    {
        return uniform;
    }

private:
    const PalettedStorage* storage = nullptr;
    std::vector<const BlockVisual*> resolved;
    const BlockVisual* uniform = nullptr;
};

struct AxisColumns {
    Columns x {};
    Columns y {};
    Columns z {};

    void fill()
    {
        for (auto* axis : { &x, &y, &z }) {
            for (auto& row : *axis) {
                row.fill(FullColumn);
            }
        }
    }

    void set(uint32_t px, uint32_t py, uint32_t pz)
    {
        x[py][pz] |= 1u << px;
        y[px][pz] |= 1u << py;
        z[px][py] |= 1u << pz;
    }

    uint32_t column(Face face, uint32_t u, uint32_t v) const
    {
        switch (face) {
        case Face::NegativeX:
        case Face::PositiveX:
            return x[v][u];
        case Face::NegativeY:
        case Face::PositiveY:
            return y[u][v];
        case Face::NegativeZ:
        case Face::PositiveZ:
            break;
        }
        return z[u][v];
    }
};

struct VisibilityMasks {
    AxisColumns geometry;
    AxisColumns occluders;
    AxisColumns leaves;
    AxisColumns cullSame;

    explicit VisibilityMasks(const PaletteFacts& facts)
    {
        if (const BlockVisual* uniform = facts.uniformVisual()) {
            if (uniform->emitsCubeGeometry()) {
                geometry.fill();
            }
            if (uniform->flags & FlagOccludesFullFace) {
                occluders.fill();
            }
            if (uniform->flags & FlagLeafModel) {
                leaves.fill();
            }
            if (uniform->flags & FlagCullSame) {
                cullSame.fill();
            }
            return;
        }
        for (uint32_t x = 0; x < Side; ++x) {
            for (uint32_t y = 0; y < Side; ++y) {
                for (uint32_t z = 0; z < Side; ++z) {
                    const BlockVisual& entry = facts.at(x, y, z);
                    if (entry.emitsCubeGeometry()) {
                        geometry.set(x, y, z);
                    }
                    if (entry.flags & FlagOccludesFullFace) {
                        occluders.set(x, y, z);
                    }
                    if (entry.flags & FlagLeafModel) {
                        leaves.set(x, y, z);
                    }
                    if (entry.flags & FlagCullSame) {
                        cullSame.set(x, y, z);
                    }
                }
            }
        }
    }
};

/**
 * Whether a face is hidden by the block beside it: a full face covers it, and
 * of the two faces two leaf blocks share only the one facing the negative way
 * is kept, so the plane between them is drawn once.
 */
bool cullsFace(Face face, uint8_t source, uint8_t neighbour)
{
    return (neighbour & FlagOccludesFullFace) || (!isNegative(face) && (source & FlagLeafModel) && (neighbour & FlagLeafModel));
}

Columns exposedColumns(Face face, const PaletteFacts& facts, const VisibilityMasks& masks, const PaletteFacts& neighbour)
{
    uint32_t boundaryBit = isNegative(face) ? 1u : (1u << (Side - 1));
    Columns exposed {};
    for (uint32_t v = 0; v < Side; ++v) {
        for (uint32_t u = 0; u < Side; ++u) {
            uint32_t geometryColumn = masks.geometry.column(face, u, v);
            uint32_t occluderColumn = masks.occluders.column(face, u, v);
            uint32_t leafColumn = masks.leaves.column(face, u, v);
            uint32_t neighbourOccluders = isNegative(face) ? occluderColumn << 1 : occluderColumn >> 1;
            uint32_t leafPairs = isNegative(face) ? 0u : leafColumn & (leafColumn >> 1);
            uint32_t faces = geometryColumn & ~neighbourOccluders & ~leafPairs & FullColumn;

            if (faces & boundaryBit) {
                uint32_t slice = isNegative(face) ? 0 : Side - 1;
                auto [sx, sy, sz] = blockCoordinate(face, slice, u, v);
                auto [nx, ny, nz] = neighbourBoundaryCoordinate(face, u, v);
                if (cullsFace(face, facts.at(sx, sy, sz).flags, neighbour.at(nx, ny, nz).flags)) {
                    faces &= ~boundaryBit;
                }
            }

            size_t faceIndex = static_cast<size_t>(face);
            uint32_t candidates = faces & masks.cullSame.column(face, u, v);
            while (candidates) {
                uint32_t slice = static_cast<uint32_t>(std::countr_zero(candidates));
                candidates &= candidates - 1;
                auto [sx, sy, sz] = blockCoordinate(face, slice, u, v);
                const BlockVisual& source = facts.at(sx, sy, sz);
                const BlockVisual* adjacent = nullptr;
                if ((1u << slice) & boundaryBit) {
                    auto [nx, ny, nz] = neighbourBoundaryCoordinate(face, u, v);
                    adjacent = &neighbour.at(nx, ny, nz);
                } else {
                    uint32_t next = isNegative(face) ? slice - 1 : slice + 1;
                    auto [nx, ny, nz] = blockCoordinate(face, next, u, v);
                    adjacent = &facts.at(nx, ny, nz);
                }
                if ((adjacent->flags & FlagCullSame) && adjacent->faces[faceIndex] == source.faces[faceIndex]) {
                    faces &= ~(1u << slice);
                }
            }
            exposed[v][u] = faces;
        }
    }
    return exposed;
}

float srgbToLinear(uint32_t channel)
{
    float value = static_cast<float>(channel) / 255.0f;
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

uint32_t linearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    float encoded = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint32_t>(encoded * 255.0f + 0.5f);
}

/**
 * Biome tint of one block: a 3x3 horizontal box blend in linear color, with
 * the birch, evergreen and dry foliage variants taken from the block itself.
 */
class TintSampler {
public:
    TintSampler(const BlockAssets& assets, const MeshInput& input)
        : assets(assets)
        , input(input)
    {
    }

    uint32_t tintWord(uint32_t materialId, uint32_t x, uint32_t y, uint32_t z)
    {
        const std::vector<Material>& materials = assets.materials();
        if (materialId >= materials.size() || materials[materialId].tintKind() == TintKind::None) {
            return 0;
        }
        const Material& material = materials[materialId];
        uint32_t domain = material.tint & (TintKindMask | TintVariantMask);
        uint32_t key = (static_cast<uint32_t>(linearIndex(x, y, z)) << 4) | domain;
        auto found = cache.find(key);
        uint32_t rgb = 0;
        if (found != cache.end()) {
            rgb = found->second;
        } else {
            rgb = blend(material.tintKind(), material.foliageVariant(), int32_t(x), int32_t(y), int32_t(z));
            cache.emplace(key, rgb);
        }
        return QuadTinted | ((material.tint & TintOverlay) ? QuadTintOverlay : 0u) | rgb;
    }

private:
    uint32_t biomeAt(int32_t x, int32_t y, int32_t z) const
    {
        int32_t dx = x < 0 ? -1 : (x >= int32_t(Side) ? 1 : 0);
        int32_t dz = z < 0 ? -1 : (z >= int32_t(Side) ? 1 : 0);
        const std::shared_ptr<const PalettedStorage>* storage = &input.biomes[size_t((dz + 1) * 3 + (dx + 1))];
        if (*storage) {
            x -= dx * int32_t(Side);
            z -= dz * int32_t(Side);
        } else {
            storage = &input.biomes[4];
            x = std::clamp(x, 0, int32_t(Side) - 1);
            z = std::clamp(z, 0, int32_t(Side) - 1);
        }
        if (!*storage) {
            return 0xFFFFFFFFu;
        }
        return (*storage)->runtimeId(uint32_t(x), uint32_t(y), uint32_t(z));
    }

    uint32_t blend(TintKind kind, FoliageVariant variant, int32_t x, int32_t y, int32_t z) const
    {
        const BiomeTints& tints = assets.biomeTints();
        if (kind == TintKind::Foliage && variant != FoliageVariant::Default) {
            return tints.colors(biomeAt(x, y, z)).domain(kind, variant);
        }
        float sum[3] = {};
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                uint32_t rgb = tints.colors(biomeAt(x + dx, y, z + dz)).domain(kind, variant);
                sum[0] += srgbToLinear((rgb >> 16) & 0xFF);
                sum[1] += srgbToLinear((rgb >> 8) & 0xFF);
                sum[2] += srgbToLinear(rgb & 0xFF);
            }
        }
        return (linearToSrgb(sum[0] / 9.0f) << 16) | (linearToSrgb(sum[1] / 9.0f) << 8) | linearToSrgb(sum[2] / 9.0f);
    }

    const BlockAssets& assets;
    const MeshInput& input;
    std::unordered_map<uint32_t, uint32_t> cache;
};

/**
 * Block and sky light solved over the 3x3x3 sub-chunk neighbourhood, which is
 * wide enough that every light reaching the center sub-chunk or its border
 * starts inside it. Sky light enters the top of every column the sky reaches,
 * stays at 15 straight down through cells that do not filter, and otherwise
 * loses the cell's filter (at least one) per step like block light.
 */
class LightField {
public:
    static constexpr int32_t Extent = 3 * int32_t(Side);
    static constexpr int32_t Offset = int32_t(Side);

    LightField(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input)
        : assets(assets)
        , ids(ids)
        , input(input)
        , retained(input.lighting)
    {
    }

    std::shared_ptr<const ChunkLighting> update(std::shared_ptr<const ChunkLighting> previous) const
    {
        if (input.isCancelled()) return {};
        size_t volume = size_t(Extent) * Extent * Extent;
        filter.assign(volume, 0);
        emission.assign(volume, 0);
        occluder.assign(volume, 0);
        loadBlocks(assets, ids, input);
        if (input.isCancelled()) return {};
        std::vector<uint8_t> seeds(volume, 0);
        std::vector<uint8_t> blocked;
        // Height of the first cell in each column that filters sky light, below the field when none does.
        std::vector<int32_t> floors(size_t(Extent) * Extent, -Offset - 1);
        if (input.skyLight) {
            blocked = blockedColumns(assets, ids, input);
            for (int32_t x = -Offset; x < Extent - Offset; ++x) {
                for (int32_t z = -Offset; z < Extent - Offset; ++z) {
                    if (blocked[size_t(x + Offset) * Extent + size_t(z + Offset)]) continue;
                    for (int32_t y = Extent - Offset - 1; y >= -Offset; --y) {
                        size_t cell = index(x, y, z);
                        seeds[cell] = static_cast<uint8_t>(15 - std::min<uint8_t>(filter[cell], 15));
                        if (filter[cell]) {
                            floors[size_t(x + Offset) * Extent + size_t(z + Offset)] = y;
                            break;
                        }
                    }
                }
            }
        }
        if (previous) {
            block = previous->block.size() == volume ? previous->block : std::vector<uint8_t>(volume, 0);
            sky = previous->sky.size() == volume ? previous->sky : std::vector<uint8_t>(volume, 0);
            relax(block, emission, previous.get(), false);
            if (input.isCancelled()) return {};
            relax(sky, seeds, previous.get(), true);
        } else {
            // Nothing to repair, so a plain flood from the sources lands on the same levels as relax
            // for a fraction of the work. This is nearly every job while the player moves around.
            spread(block, emission);
            if (input.isCancelled()) return {};
            sky = seeds;
            std::vector<uint32_t> sources = input.skyLight ? skySources(blocked, floors) : std::vector<uint32_t> {};
            propagate(sky, sources);
        }
        if (input.isCancelled()) return {};
        auto result = std::make_shared<ChunkLighting>();
        result->filter = std::move(filter);
        result->emission = std::move(emission);
        result->occluder = std::move(occluder);
        result->block = std::move(block);
        result->sky = std::move(sky);
        result->skySeeds = std::move(seeds);
        return result;
    }

    uint8_t blockAt(int32_t x, int32_t y, int32_t z) const
    {
        solve();
        return inside(x, y, z) ? (retained ? retained->block : block)[index(x, y, z)] : 0;
    }

    uint8_t skyAt(int32_t x, int32_t y, int32_t z) const
    {
        solve();
        return inside(x, y, z) ? (retained ? retained->sky : sky)[index(x, y, z)] : 0;
    }

    bool occludes(int32_t x, int32_t y, int32_t z) const
    {
        solve();
        return inside(x, y, z) && (retained ? retained->occluder : occluder)[index(x, y, z)] != 0;
    }

    /**
     * Corner lighting of a quad on the given face of block (x, y, z), with
     * corner positions in 1/256 block: each corner of the whole face averages
     * the four cells in front of it and counts the full blocks beside and
     * diagonal to it, and a quad corner blends those by where it sits, so a
     * half face like a slab side lines up with the full faces next to it.
     */
    QuadLight bake(int32_t x, int32_t y, int32_t z, Face face, const std::array<std::array<int32_t, 3>, 4>& positions) const
    {
        std::array<int32_t, 3> normal {};
        size_t tangentA = 0;
        size_t tangentB = 0;
        switch (face) {
        case Face::NegativeX:
            normal = { -1, 0, 0 };
            tangentA = 1;
            tangentB = 2;
            break;
        case Face::PositiveX:
            normal = { 1, 0, 0 };
            tangentA = 1;
            tangentB = 2;
            break;
        case Face::NegativeY:
            normal = { 0, -1, 0 };
            tangentA = 0;
            tangentB = 2;
            break;
        case Face::PositiveY:
            normal = { 0, 1, 0 };
            tangentA = 0;
            tangentB = 2;
            break;
        case Face::NegativeZ:
            normal = { 0, 0, -1 };
            tangentA = 0;
            tangentB = 1;
            break;
        case Face::PositiveZ:
            normal = { 0, 0, 1 };
            tangentA = 0;
            tangentB = 1;
            break;
        }
        size_t axis = normal[0] != 0 ? 0 : normal[1] != 0 ? 1 : 2;
        int32_t plane = normal[axis] < 0 ? 0 : 256;
        bool flush = std::all_of(positions.begin(), positions.end(), [&](const std::array<int32_t, 3>& position) {
            return position[axis] == plane;
        });
        std::array<int32_t, 3> self { x, y, z };
        std::array<int32_t, 3> outward { x + normal[0], y + normal[1], z + normal[2] };
        // Faces sunk into their block, like the step of a stair, take their
        // light from the block's own layer rather than the one beyond.
        const std::array<int32_t, 3>& base = flush ? outward : self;
        const std::array<int32_t, 3>& center = flush || !occludes(outward[0], outward[1], outward[2]) ? outward : self;
        uint32_t centerLevel = levelAt(center);

        // Light sums and occlusion at the four corners of the whole face,
        // indexed by which side of each tangent they sit on.
        std::array<std::array<uint32_t, 3>, 4> full {};
        for (size_t index = 0; index < 4; ++index) {
            int32_t signA = (index & 1) ? 1 : -1;
            int32_t signB = (index & 2) ? 1 : -1;
            std::array<int32_t, 3> sideA = base;
            sideA[tangentA] += signA;
            std::array<int32_t, 3> sideB = base;
            sideB[tangentB] += signB;
            std::array<int32_t, 3> diagonal = sideA;
            diagonal[tangentB] += signB;
            bool occludedA = occludes(sideA[0], sideA[1], sideA[2]);
            bool occludedB = occludes(sideB[0], sideB[1], sideB[2]);
            bool occludedDiagonal = occludes(diagonal[0], diagonal[1], diagonal[2]);
            uint32_t ao = occludedA && occludedB ? 3u : uint32_t(occludedA) + uint32_t(occludedB) + uint32_t(occludedDiagonal);
            uint32_t blockSum = 0;
            uint32_t skySum = 0;
            for (const std::array<int32_t, 3>& sample : { center, sideA, sideB, occludedA && occludedB ? sideA : diagonal }) {
                // A dark sample is usually a solid block; the game counts it as
                // the center instead, so walls only darken through occlusion.
                uint32_t level = sample == center ? centerLevel : levelAt(sample);
                if (level == 0) {
                    level = centerLevel;
                }
                blockSum += level & 15u;
                skySum += level >> 4;
            }
            full[index] = { blockSum, skySum, ao };
        }

        QuadLight result;
        for (size_t corner = 0; corner < 4; ++corner) {
            float u = std::clamp(float(positions[corner][tangentA]) / 256.0f, 0.0f, 1.0f);
            float v = std::clamp(float(positions[corner][tangentB]) / 256.0f, 0.0f, 1.0f);
            std::array<float, 4> weights { (1.0f - u) * (1.0f - v), u * (1.0f - v), (1.0f - u) * v, u * v };
            std::array<float, 3> mixed {};
            for (size_t index = 0; index < 4; ++index) {
                for (size_t channel = 0; channel < 3; ++channel) {
                    mixed[channel] += float(full[index][channel]) * weights[index];
                }
            }
            uint32_t blockLevel = uint32_t(mixed[0] / 4.0f + 0.001f);
            uint32_t skyLevel = uint32_t(mixed[1] / 4.0f + 0.001f);
            uint32_t ao = uint32_t(mixed[2] + 0.5f);
            result.light |= (blockLevel | (skyLevel << 4)) << (corner * 8);
            result.ao |= ao << (corner * 2);
        }
        return result;
    }

    QuadLight flat(int32_t x, int32_t y, int32_t z) const
    {
        uint32_t sample = uint32_t(blockAt(x, y, z)) | (uint32_t(skyAt(x, y, z)) << 4);
        return { sample | (sample << 8) | (sample << 16) | (sample << 24), 0 };
    }

private:
    uint32_t levelAt(const std::array<int32_t, 3>& cell) const
    {
        return uint32_t(blockAt(cell[0], cell[1], cell[2])) | (uint32_t(skyAt(cell[0], cell[1], cell[2])) << 4);
    }

    /**
     * Solves the field on first use, so a sub-chunk that emits no face never
     * pays for it.
     */
    void solve() const
    {
        if (retained || solved) {
            return;
        }
        solved = true;
        size_t volume = size_t(Extent) * Extent * Extent;
        filter.assign(volume, 0);
        emission.assign(volume, 0);
        occluder.assign(volume, 0);
        block.assign(volume, 0);
        sky.assign(volume, 0);
        loadBlocks(assets, ids, input);
        solveBlock();
        if (input.skyLight) {
            solveSky(assets, ids, input);
        }
    }

    static bool inside(int32_t x, int32_t y, int32_t z)
    {
        return x >= -Offset && y >= -Offset && z >= -Offset && x < Extent - Offset && y < Extent - Offset && z < Extent - Offset;
    }

    static size_t index(int32_t x, int32_t y, int32_t z)
    {
        return (size_t(x + Offset) * Extent + size_t(y + Offset)) * Extent + size_t(z + Offset);
    }

    static void cellOf(size_t cell, int32_t& x, int32_t& y, int32_t& z)
    {
        z = int32_t(cell % Extent) - Offset;
        y = int32_t((cell / Extent) % Extent) - Offset;
        x = int32_t(cell / (size_t(Extent) * Extent)) - Offset;
    }

    void loadBlocks(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input) const
    {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dz = -1; dz <= 1; ++dz) {
                    const SubChunk* subChunk = (dx == 0 && dy == 0 && dz == 0) ? input.center.get() : input.around[size_t((dx + 1) * 9 + (dy + 1) * 3 + (dz + 1))].get();
                    if (!subChunk) {
                        continue;
                    }
                    for (const PalettedStorage& storage : subChunk->storages()) {
                        std::vector<const BlockVisual*> palette;
                        for (uint32_t value : storage.palette()) {
                            palette.push_back(value == ImplicitAir || hiddenValue(ids, value) ? nullptr : &assets.visual(value, ids.hashed, ids.sequential.get()));
                        }
                        bool first = &storage == &subChunk->storages().front();
                        for (uint32_t x = 0; x < Side; ++x) {
                            for (uint32_t y = 0; y < Side; ++y) {
                                for (uint32_t z = 0; z < Side; ++z) {
                                    const BlockVisual* visual = palette[storage.isUniform() ? 0 : storage.paletteIndex(linearIndex(x, y, z))];
                                    if (!visual) {
                                        continue;
                                    }
                                    size_t cell = index(dx * int32_t(Side) + int32_t(x), dy * int32_t(Side) + int32_t(y), dz * int32_t(Side) + int32_t(z));
                                    filter[cell] = std::max(filter[cell], visual->lightFilter);
                                    emission[cell] = std::max(emission[cell], visual->lightEmission);
                                    if (first && (visual->flags & FlagOccludesFullFace)) {
                                        occluder[cell] = 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    void relax(std::vector<uint8_t>& levels, const std::vector<uint8_t>& seeds, const ChunkLighting* previous, bool skyChannel) const
    {
        const size_t volume = levels.size();
        std::vector<uint8_t> queued(volume, 0);
        std::deque<uint32_t> queue;
        auto enqueue = [&](size_t cell) {
            if (!queued[cell]) { queued[cell] = 1; queue.push_back(static_cast<uint32_t>(cell)); }
        };
        bool validPrevious = previous && previous->filter.size() == volume
            && (skyChannel ? previous->skySeeds.size() == volume : previous->emission.size() == volume);
        const std::vector<uint8_t>* oldSeeds = validPrevious ? &(skyChannel ? previous->skySeeds : previous->emission) : nullptr;
        for (size_t cell = 0; cell < volume; ++cell) {
            if ((!validPrevious && seeds[cell]) || (validPrevious && (filter[cell] != previous->filter[cell] || seeds[cell] != (*oldSeeds)[cell]))) enqueue(cell);
        }
        static constexpr int32_t Steps[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
        while (!queue.empty()) {
            if (input.isCancelled()) return;
            size_t cell = queue.front(); queue.pop_front(); queued[cell] = 0;
            int32_t x, y, z; cellOf(cell, x, y, z);
            uint8_t nextLevel = std::min<uint8_t>(seeds[cell], 15);
            for (const auto& step : Steps) {
                int32_t nx = x + step[0], ny = y + step[1], nz = z + step[2];
                if (!inside(nx, ny, nz)) continue;
                int candidate = int(levels[index(nx, ny, nz)]) - std::max<int>(filter[cell], 1);
                if (candidate > nextLevel) nextLevel = static_cast<uint8_t>(candidate);
            }
            if (nextLevel == levels[cell]) continue;
            levels[cell] = nextLevel;
            for (const auto& step : Steps) {
                int32_t nx = x + step[0], ny = y + step[1], nz = z + step[2];
                if (inside(nx, ny, nz)) enqueue(index(nx, ny, nz));
            }
        }
    }

    void propagate(std::vector<uint8_t>& levels, std::vector<uint32_t>& queue) const
    {
        static constexpr int32_t Steps[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
        for (size_t head = 0; head < queue.size(); ++head) {
            uint32_t cell = queue[head];
            uint8_t source = levels[cell];
            if (source <= 1) {
                continue;
            }
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
            cellOf(cell, x, y, z);
            for (const auto& step : Steps) {
                int32_t nx = x + step[0];
                int32_t ny = y + step[1];
                int32_t nz = z + step[2];
                if (!inside(nx, ny, nz)) {
                    continue;
                }
                size_t next = index(nx, ny, nz);
                int32_t candidate = int32_t(source) - std::max<int32_t>(filter[next], 1);
                if (candidate > int32_t(levels[next])) {
                    levels[next] = static_cast<uint8_t>(candidate);
                    queue.push_back(static_cast<uint32_t>(next));
                }
            }
        }
    }

    void spread(std::vector<uint8_t>& levels, const std::vector<uint8_t>& seeds) const
    {
        levels.assign(seeds.size(), 0);
        std::vector<uint32_t> queue;
        for (size_t cell = 0; cell < seeds.size(); ++cell) {
            if (seeds[cell] > 0) {
                levels[cell] = std::min<uint8_t>(seeds[cell], 15);
                queue.push_back(static_cast<uint32_t>(cell));
            }
        }
        propagate(levels, queue);
    }

    /**
     * The open sky cells that can light anything. A cell with full sky light only brightens
     * a neighbour that sits at or under the top filtering cell of its own column, so in each
     * column that is the stretch from its own floor up to the highest neighbouring floor.
     * Flooding from every open cell instead lands on the same levels, just several times slower.
     */
    std::vector<uint32_t> skySources(const std::vector<uint8_t>& blocked, const std::vector<int32_t>& floors) const
    {
        const int32_t top = Extent - Offset - 1;
        auto reachOf = [&](int32_t x, int32_t z) {
            if (x < -Offset || z < -Offset || x > top || z > top) return -Offset - 1;
            size_t column = size_t(x + Offset) * Extent + size_t(z + Offset);
            return blocked[column] ? top : floors[column];
        };
        std::vector<uint32_t> sources;
        for (int32_t x = -Offset; x <= top; ++x) {
            for (int32_t z = -Offset; z <= top; ++z) {
                size_t column = size_t(x + Offset) * Extent + size_t(z + Offset);
                if (blocked[column]) continue;
                int32_t floor = floors[column];
                int32_t reach = std::min(top, std::max({ reachOf(x - 1, z), reachOf(x + 1, z), reachOf(x, z - 1), reachOf(x, z + 1) }));
                for (int32_t y = std::max(floor, -Offset); y <= reach; ++y) {
                    sources.push_back(static_cast<uint32_t>(index(x, y, z)));
                }
                if (floor >= -Offset && floor > reach) {
                    sources.push_back(static_cast<uint32_t>(index(x, floor, z)));
                }
            }
        }
        return sources;
    }

    void solveBlock() const
    {
        std::vector<uint32_t> queue;
        for (size_t cell = 0; cell < emission.size(); ++cell) {
            if (emission[cell] > 0) {
                block[cell] = emission[cell];
                queue.push_back(static_cast<uint32_t>(cell));
            }
        }
        propagate(block, queue);
    }

    static std::vector<uint8_t> blockedColumns(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input)
    {
        std::vector<uint8_t> blocked(size_t(Extent) * Extent, 0);
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (int32_t dz = -1; dz <= 1; ++dz) {
                for (const std::shared_ptr<const SubChunk>& subChunk : input.above[size_t((dx + 1) * 3 + (dz + 1))]) {
                    if (!subChunk) {
                        continue;
                    }
                    for (const PalettedStorage& storage : subChunk->storages()) {
                        std::vector<uint8_t> filters;
                        bool anyFilter = false;
                        for (uint32_t value : storage.palette()) {
                            bool filtering = value != ImplicitAir && !hiddenValue(ids, value) && assets.visual(value, ids.hashed, ids.sequential.get()).lightFilter > 0;
                            filters.push_back(filtering ? 1 : 0);
                            anyFilter |= filtering;
                        }
                        if (!anyFilter) {
                            continue;
                        }
                        for (uint32_t x = 0; x < Side; ++x) {
                            for (uint32_t z = 0; z < Side; ++z) {
                                uint8_t& column = blocked[size_t(dx * int32_t(Side) + int32_t(x) + Offset) * Extent + size_t(dz * int32_t(Side) + int32_t(z) + Offset)];
                                if (column) {
                                    continue;
                                }
                                for (uint32_t y = 0; y < Side && !column; ++y) {
                                    column = filters[storage.isUniform() ? 0 : storage.paletteIndex(linearIndex(x, y, z))];
                                }
                            }
                        }
                    }
                }
            }
        }
        return blocked;
    }

    void solveSky(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input) const
    {
        std::vector<uint32_t> queue;
        std::vector<uint8_t> blocked = blockedColumns(assets, ids, input);
        int32_t top = Extent - Offset - 1;
        for (int32_t x = -Offset; x < Extent - Offset; ++x) {
            for (int32_t z = -Offset; z < Extent - Offset; ++z) {
                if (blocked[size_t(x + Offset) * Extent + size_t(z + Offset)]) {
                    continue;
                }
                for (int32_t y = top; y >= -Offset; --y) {
                    size_t cell = index(x, y, z);
                    if (filter[cell] == 0) {
                        sky[cell] = 15;
                        queue.push_back(static_cast<uint32_t>(cell));
                        continue;
                    }
                    sky[cell] = static_cast<uint8_t>(15 - filter[cell]);
                    if (sky[cell] > 0) {
                        queue.push_back(static_cast<uint32_t>(cell));
                    }
                    break;
                }
            }
        }
        propagate(sky, queue);
    }

    const BlockAssets& assets;
    const IdMapping& ids;
    const MeshInput& input;
    std::shared_ptr<const ChunkLighting> retained;
    mutable bool solved = false;
    mutable std::vector<uint8_t> filter;
    mutable std::vector<uint8_t> emission;
    mutable std::vector<uint8_t> occluder;
    mutable std::vector<uint8_t> block;
    mutable std::vector<uint8_t> sky;
};

std::array<std::array<int32_t, 3>, 4> cubeFaceCorners(Face face)
{
    switch (face) {
    case Face::NegativeX:
        return { { { 0, 0, 0 }, { 0, 0, 256 }, { 0, 256, 256 }, { 0, 256, 0 } } };
    case Face::PositiveX:
        return { { { 256, 0, 0 }, { 256, 256, 0 }, { 256, 256, 256 }, { 256, 0, 256 } } };
    case Face::NegativeY:
        return { { { 0, 0, 0 }, { 256, 0, 0 }, { 256, 0, 256 }, { 0, 0, 256 } } };
    case Face::PositiveY:
        return { { { 0, 256, 0 }, { 0, 256, 256 }, { 256, 256, 256 }, { 256, 256, 0 } } };
    case Face::NegativeZ:
        return { { { 0, 0, 0 }, { 0, 256, 0 }, { 256, 256, 0 }, { 256, 0, 0 } } };
    case Face::PositiveZ:
        break;
    }
    return { { { 0, 0, 256 }, { 256, 0, 256 }, { 256, 256, 256 }, { 0, 256, 256 } } };
}

void greedySlice(const PaletteFacts& facts, TintSampler& tints, const LightField& field, Face face, uint32_t slice, std::array<uint32_t, Side>& rows, std::vector<PackedQuad>& opaque, std::vector<PackedQuad>& translucent)
{
    const std::array<std::array<int32_t, 3>, 4> corners = cubeFaceCorners(face);
    std::unordered_map<size_t, QuadLight> baked;
    auto lightOf = [&](uint32_t x, uint32_t y, uint32_t z) {
        auto [found, inserted] = baked.try_emplace(linearIndex(x, y, z));
        if (inserted) {
            found->second = field.bake(int32_t(x), int32_t(y), int32_t(z), face, corners);
        }
        return found->second;
    };
    size_t faceIndex = static_cast<size_t>(face);
    for (uint32_t v = 0; v < Side; ++v) {
        while (rows[v] != 0) {
            uint32_t u = static_cast<uint32_t>(std::countr_zero(rows[v]));
            auto origin = blockCoordinate(face, slice, u, v);
            bool isTranslucent = facts.at(origin[0], origin[1], origin[2]).flags & FlagTranslucent;
            uint32_t material = facts.at(origin[0], origin[1], origin[2]).faces[faceIndex];
            uint32_t tint = tints.tintWord(material, origin[0], origin[1], origin[2]);
            QuadLight lighting = lightOf(origin[0], origin[1], origin[2]);
            auto matches = [&](uint32_t x, uint32_t y, uint32_t z) {
                const auto& visual = facts.at(x, y, z);
                return bool(visual.flags & FlagTranslucent) == isTranslucent && visual.faces[faceIndex] == material
                    && (tint == 0 || tints.tintWord(material, x, y, z) == tint) && lightOf(x, y, z) == lighting;
            };

            uint32_t shifted = rows[v] >> u;
            uint32_t binaryWidth = std::min<uint32_t>(static_cast<uint32_t>(std::countr_one(shifted)), Side - u);
            uint32_t width = 1;
            // Transparent faces are sorted by their centers. Merging them across
            // blocks breaks their ordering against water and other nearby faces.
            while (!isTranslucent && width < binaryWidth) {
                auto [x, y, z] = blockCoordinate(face, slice, u + width, v);
                if (!matches(x, y, z)) {
                    break;
                }
                ++width;
            }

            uint32_t span = ((width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u))) << u;
            uint32_t height = 1;
            while (!isTranslucent && v + height < Side && (rows[v + height] & span) == span) {
                bool same = true;
                for (uint32_t offset = 0; offset < width && same; ++offset) {
                    auto [x, y, z] = blockCoordinate(face, slice, u + offset, v + height);
                    same = matches(x, y, z);
                }
                if (!same) {
                    break;
                }
                ++height;
            }

            for (uint32_t row = v; row < v + height; ++row) {
                rows[row] &= ~span;
            }
            (isTranslucent ? translucent : opaque).push_back(PackedQuad::make(origin[0], origin[1], origin[2], face, width, height, material, tint, lighting));
        }
    }
}

class ModelContext {
public:
    ModelContext(const BlockAssets& assets, const PaletteFacts& facts, const std::array<const PaletteFacts*, 6>& neighbours)
        : assets(assets)
        , facts(facts)
        , neighbours(neighbours)
    {
    }

    const BlockVisual& adjacent(uint32_t x, uint32_t y, uint32_t z, Face face) const
    {
        int32_t nx = int32_t(x);
        int32_t ny = int32_t(y);
        int32_t nz = int32_t(z);
        switch (face) {
        case Face::NegativeX:
            --nx;
            break;
        case Face::PositiveX:
            ++nx;
            break;
        case Face::NegativeY:
            --ny;
            break;
        case Face::PositiveY:
            ++ny;
            break;
        case Face::NegativeZ:
            --nz;
            break;
        case Face::PositiveZ:
            ++nz;
            break;
        }
        auto inside = [](int32_t value) {
            return value >= 0 && value < int32_t(Side);
        };
        if (inside(nx) && inside(ny) && inside(nz)) {
            return facts.at(uint32_t(nx), uint32_t(ny), uint32_t(nz));
        }
        auto wrap = [](int32_t value) {
            return uint32_t((value + int32_t(Side)) % int32_t(Side));
        };
        return neighbours[static_cast<size_t>(face)]->at(wrap(nx), wrap(ny), wrap(nz));
    }

    uint32_t flagsOf(const BlockVisual& visual) const
    {
        return assets.templateFlags(visual);
    }

    uint32_t connectedMask(uint32_t x, uint32_t y, uint32_t z, uint32_t connectionFlag) const
    {
        static constexpr std::pair<Face, uint32_t> Directions[] = {
            { Face::NegativeZ, 1 },
            { Face::PositiveX, 2 },
            { Face::PositiveZ, 4 },
            { Face::NegativeX, 8 },
        };
        uint32_t mask = 0;
        for (auto [face, bit] : Directions) {
            const BlockVisual& neighbour = adjacent(x, y, z, face);
            uint32_t neighbourFlags = flagsOf(neighbour);
            bool connects = false;
            if (connectionFlag == TemplatePane) {
                connects = (neighbourFlags & (TemplatePane | TemplateWall)) || (neighbour.flags & FlagOccludesFullFace);
            } else {
                bool gate = (face == Face::NegativeZ || face == Face::PositiveZ) ? (neighbourFlags & TemplateGateAxisX) : (neighbourFlags & TemplateGateAxisZ);
                connects = (neighbourFlags & connectionFlag) || gate || (neighbour.flags & FlagOccludesFullFace);
            }
            if (connects) {
                mask |= bit;
            }
        }
        return mask;
    }

    std::optional<std::pair<uint32_t, bool>> stairSignature(const BlockVisual& visual) const
    {
        if (!(flagsOf(visual) & TemplateStair)) {
            return std::nullopt;
        }
        return std::make_pair(((visual.variant & 3) + 2) & 3, (visual.variant & 4) != 0);
    }

    static Face stairFace(uint32_t facing)
    {
        switch (facing & 3) {
        case 0:
            return Face::PositiveZ;
        case 1:
            return Face::NegativeX;
        case 2:
            return Face::NegativeZ;
        default:
            return Face::PositiveX;
        }
    }

    uint32_t stairTemplate(uint32_t x, uint32_t y, uint32_t z, const BlockVisual& visual) const
    {
        auto signature = stairSignature(visual);
        if (!signature) {
            return visual.modelTemplate;
        }
        auto [facing, upsideDown] = *signature;
        uint32_t rotatedFacing = (facing + 1) & 3;
        auto closed = stairSignature(adjacent(x, y, z, stairFace(facing)));
        if (closed && closed->second == upsideDown) {
            if (closed->first == rotatedFacing) {
                return visual.modelTemplate + 4;
            }
            if (closed->first == ((rotatedFacing + 2) & 3)) {
                auto side = stairSignature(adjacent(x, y, z, stairFace(rotatedFacing)));
                if (side != signature) {
                    return visual.modelTemplate + 3;
                }
                return visual.modelTemplate;
            }
        }
        auto open = stairSignature(adjacent(x, y, z, stairFace((facing + 2) & 3)));
        if (open && open->second == upsideDown) {
            if (open->first == rotatedFacing) {
                auto side = stairSignature(adjacent(x, y, z, stairFace(rotatedFacing)));
                if (side != signature) {
                    return visual.modelTemplate + 1;
                }
            } else if (open->first == ((rotatedFacing + 2) & 3)) {
                return visual.modelTemplate + 2;
            }
        }
        return visual.modelTemplate;
    }

    std::array<uint32_t, 2> selectTemplates(uint32_t x, uint32_t y, uint32_t z, const BlockVisual& visual, uint32_t& count) const
    {
        uint32_t flags = flagsOf(visual);
        count = 1;
        if (flags & TemplatePane) {
            return { visual.modelTemplate + connectedMask(x, y, z, TemplatePane), NoModelTemplate };
        }
        uint32_t fenceFlag = flags & (TemplateFenceWood | TemplateFenceNether);
        if (fenceFlag) {
            uint32_t mask = connectedMask(x, y, z, fenceFlag);
            if (mask) {
                count = 2;
                return { visual.modelTemplate, visual.modelTemplate + 1 + mask };
            }
            return { visual.modelTemplate, NoModelTemplate };
        }
        return { stairTemplate(x, y, z, visual), NoModelTemplate };
    }

private:
    const BlockAssets& assets;
    const PaletteFacts& facts;
    const std::array<const PaletteFacts*, 6>& neighbours;
};

std::optional<Face> faceFromId(uint32_t id)
{
    switch (id) {
    case 1:
        return Face::NegativeY;
    case 2:
        return Face::PositiveY;
    case 3:
        return Face::NegativeX;
    case 4:
        return Face::PositiveX;
    case 5:
        return Face::NegativeZ;
    case 6:
        return Face::PositiveZ;
    default:
        return std::nullopt;
    }
}

Face rotateFace(Face face, uint32_t rotation)
{
    for (uint32_t i = 0; i < (rotation & 3); ++i) {
        switch (face) {
        case Face::NegativeX:
            face = Face::NegativeZ;
            break;
        case Face::PositiveX:
            face = Face::PositiveZ;
            break;
        case Face::NegativeZ:
            face = Face::PositiveX;
            break;
        case Face::PositiveZ:
            face = Face::NegativeX;
            break;
        default:
            break;
        }
    }
    return face;
}

void emitModelQuad(const ModelQuad& quad, uint32_t x, uint32_t y, uint32_t z, uint32_t rotation, uint32_t tint, const LightField& field, std::vector<ModelQuadGpu>& out)
{
    ModelQuadGpu gpu;
    std::array<int16_t, 12> positions {};
    std::array<std::array<int32_t, 3>, 4> local {};
    for (size_t corner = 0; corner < 4; ++corner) {
        int32_t px = quad.positions[corner][0] - 128;
        int32_t py = quad.positions[corner][1];
        int32_t pz = quad.positions[corner][2] - 128;
        for (uint32_t i = 0; i < (rotation & 3); ++i) {
            int32_t rotated = -pz;
            pz = px;
            px = rotated;
        }
        local[corner] = { px + 128, py, pz + 128 };
        positions[corner * 3 + 0] = static_cast<int16_t>(px + 128 + int32_t(x) * 256);
        positions[corner * 3 + 1] = static_cast<int16_t>(py + int32_t(y) * 256);
        positions[corner * 3 + 2] = static_cast<int16_t>(pz + 128 + int32_t(z) * 256);
    }
    for (size_t word = 0; word < 6; ++word) {
        gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
    }
    for (size_t corner = 0; corner < 4; ++corner) {
        gpu.words[6 + corner] = uint32_t(quad.uvs[corner][0]) | (uint32_t(quad.uvs[corner][1]) << 16);
    }
    gpu.words[10] = quad.material;
    std::optional<Face> face = faceFromId(quad.flags & QuadFaceMask);
    uint32_t shade = face ? uint32_t(rotateFace(*face, rotation)) + 1 : 0;
    uint32_t rgb = (tint & QuadTinted) ? std::max(tint & 0xFFFFFFu, 1u) : 0u;
    gpu.words[11] = shade | (rgb << 8);
    QuadLight lighting = face ? field.bake(int32_t(x), int32_t(y), int32_t(z), rotateFace(*face, rotation), local) : field.flat(int32_t(x), int32_t(y), int32_t(z));
    gpu.words[12] = lighting.light;
    gpu.words[13] = lighting.ao;
    out.push_back(gpu);
}

void meshBlockEntity(const BlockAssets& assets, const BlockVisual& visual, const MeshInput& input, uint32_t x, uint32_t y, uint32_t z, TintSampler& tints, const LightField& field, std::vector<ModelQuadGpu>& out)
{
    const Tag* data = nullptr;
    if (input.blockEntities) {
        auto found = input.blockEntities->find(static_cast<uint16_t>(linearIndex(x, y, z)));
        if (found != input.blockEntities->end()) {
            data = &found->second;
        }
    }
    std::array<int32_t, 3> position { input.origin[0] + int32_t(x), input.origin[1] + int32_t(y), input.origin[2] + int32_t(z) };
    uint32_t selected = assets.blockEntityTemplate(visual, data, position);
    const std::vector<ModelTemplate>& templates = assets.modelTemplates();
    if (selected >= templates.size()) {
        return;
    }
    const ModelTemplate& modelTemplate = templates[selected];
    for (uint32_t q = 0; q < modelTemplate.quadCount; ++q) {
        const ModelQuad& quad = assets.modelQuads()[modelTemplate.quadStart + q];
        emitModelQuad(quad, x, y, z, 0, tints.tintWord(quad.material, x, y, z), field, out);
    }
}

void meshModels(const BlockAssets& assets, const MeshInput& input, const PaletteFacts& facts, const std::array<const PaletteFacts*, 6>& neighbours, TintSampler& tints, const LightField& field, std::vector<ModelQuadGpu>& opaque, std::vector<ModelQuadGpu>& translucent)
{
    if (const BlockVisual* uniform = facts.uniformVisual(); uniform && !uniform->hasModel()) {
        return;
    }
    ModelContext context(assets, facts, neighbours);
    const std::vector<ModelTemplate>& templates = assets.modelTemplates();
    const std::vector<ModelQuad>& quads = assets.modelQuads();
    for (uint32_t x = 0; x < Side; ++x) {
        if (input.isCancelled()) return;
        for (uint32_t y = 0; y < Side; ++y) {
            for (uint32_t z = 0; z < Side; ++z) {
                const BlockVisual& visual = facts.at(x, y, z);
                if (!visual.hasModel()) {
                    continue;
                }
                if (visual.blockEntity != EntityNone) {
                    meshBlockEntity(assets, visual, input, x, y, z, tints, field, opaque);
                    continue;
                }
                uint32_t count = 0;
                std::array<uint32_t, 2> selected = context.selectTemplates(x, y, z, visual, count);
                uint32_t rotation = visual.variant & 3;
                for (uint32_t t = 0; t < count; ++t) {
                    if (selected[t] >= templates.size()) {
                        continue;
                    }
                    const ModelTemplate& modelTemplate = templates[selected[t]];
                    for (uint32_t q = 0; q < modelTemplate.quadCount; ++q) {
                        const ModelQuad& quad = quads[modelTemplate.quadStart + q];
                        if (std::optional<Face> cull = faceFromId((quad.flags & QuadCullFaceMask) >> 4)) {
                            Face cullFace = rotateFace(*cull, rotation);
                            const BlockVisual& neighbour = context.adjacent(x, y, z, cullFace);
                            bool equalPane = (modelTemplate.flags & TemplatePane) && (context.flagsOf(neighbour) & TemplatePane) && neighbour.faces == visual.faces;
                            if ((neighbour.flags & FlagOccludesFullFace) || equalPane) {
                                continue;
                            }
                        }
                        emitModelQuad(quad, x, y, z, rotation, tints.tintWord(quad.material, x, y, z), field, (visual.flags & FlagTranslucent) ? translucent : opaque);
                    }
                }
            }
        }
    }
}

/**
 * Liquid surfaces: per-corner heights from the surrounding liquid levels, a
 * flow gradient that orients the flowing texture, and faces only where the
 * liquid meets something that is neither the same liquid nor a full block.
 */
class LiquidMesher {
public:
    LiquidMesher(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input, TintSampler& tints, const LightField& field)
        : assets(assets)
        , ids(ids)
        , input(input)
        , tints(tints)
        , field(field)
    {
    }

    void mesh(std::vector<ModelQuadGpu>& opaque, std::vector<ModelQuadGpu>& translucent)
    {
        if (!containsLiquid()) {
            return;
        }
        for (int32_t x = 0; x < int32_t(Side); ++x) {
            if (input.isCancelled()) return;
            for (int32_t y = 0; y < int32_t(Side); ++y) {
                for (int32_t z = 0; z < int32_t(Side); ++z) {
                    std::optional<Cell> cell = liquid(x, y, z);
                    if (!cell) {
                        continue;
                    }
                    meshCell(x, y, z, *cell, cell->kind == 2 ? opaque : translucent);
                }
            }
        }
    }

private:
    struct Cell {
        uint8_t kind = 0;
        uint8_t depth = 0;
        uint8_t height = 0;
        bool falling = false;
        std::array<uint32_t, 6> faces {};

        uint8_t effectiveDepth() const
        {
            return falling ? 0 : depth;
        }
    };

    bool containsLiquid() const
    {
        if (!input.center) {
            return false;
        }
        for (const PalettedStorage& storage : input.center->storages()) {
            for (uint32_t value : storage.palette()) {
                if (value != ImplicitAir && !hiddenValue(ids, value) && assets.visual(value, ids.hashed, ids.sequential.get()).liquid) {
                    return true;
                }
            }
        }
        return false;
    }

    const SubChunk* subChunkAt(int32_t& x, int32_t& y, int32_t& z) const
    {
        int32_t dx = x < 0 ? -1 : (x >= int32_t(Side) ? 1 : 0);
        int32_t dy = y < 0 ? -1 : (y >= int32_t(Side) ? 1 : 0);
        int32_t dz = z < 0 ? -1 : (z >= int32_t(Side) ? 1 : 0);
        x -= dx * int32_t(Side);
        y -= dy * int32_t(Side);
        z -= dz * int32_t(Side);
        if (dx == 0 && dy == 0 && dz == 0) {
            return input.center.get();
        }
        return input.around[size_t((dx + 1) * 9 + (dy + 1) * 3 + (dz + 1))].get();
    }

    const BlockVisual* layer(int32_t x, int32_t y, int32_t z, size_t index) const
    {
        const SubChunk* subChunk = subChunkAt(x, y, z);
        if (!subChunk || index >= subChunk->storages().size()) {
            return nullptr;
        }
        uint32_t value = subChunk->storages()[index].runtimeId(uint32_t(x), uint32_t(y), uint32_t(z));
        if (value == ImplicitAir || hiddenValue(ids, value)) {
            return nullptr;
        }
        return &assets.visual(value, ids.hashed, ids.sequential.get());
    }

    std::optional<Cell> liquid(int32_t x, int32_t y, int32_t z) const
    {
        for (size_t index = 0; index < 2; ++index) {
            const BlockVisual* visual = layer(x, y, z, index);
            if (visual && visual->liquid) {
                Cell cell;
                cell.kind = visual->liquid;
                cell.falling = visual->liquidLevel >= 8;
                cell.depth = visual->liquidLevel & 7;
                cell.height = cell.falling ? 227 : static_cast<uint8_t>(((8 - cell.depth) * 255 + 4) / 9);
                cell.faces = visual->faces;
                return cell;
            }
        }
        return std::nullopt;
    }

    bool compatible(int32_t x, int32_t y, int32_t z, uint8_t kind) const
    {
        std::optional<Cell> cell = liquid(x, y, z);
        return cell && cell->kind == kind;
    }

    bool solid(int32_t x, int32_t y, int32_t z) const
    {
        const BlockVisual* visual = layer(x, y, z, 0);
        return visual && (visual->flags & FlagOccludesFullFace);
    }

    bool open(int32_t x, int32_t y, int32_t z) const
    {
        return !liquid(x, y, z) && !solid(x, y, z);
    }

    std::array<uint8_t, 4> cornerHeights(int32_t x, int32_t y, int32_t z, uint8_t kind) const
    {
        static constexpr int32_t Corners[4][4][2] = {
            { { 0, 0 }, { -1, 0 }, { 0, -1 }, { -1, -1 } },
            { { 0, 0 }, { 1, 0 }, { 0, -1 }, { 1, -1 } },
            { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
            { { 0, 0 }, { -1, 0 }, { 0, 1 }, { -1, 1 } },
        };
        std::array<uint8_t, 4> heights {};
        for (size_t corner = 0; corner < 4; ++corner) {
            const auto& samples = Corners[corner];
            bool includeDiagonal = compatible(x + samples[1][0], y, z + samples[1][1], kind) || compatible(x + samples[2][0], y, z + samples[2][1], kind);
            size_t count = includeDiagonal ? 4 : 3;
            bool covered = false;
            for (size_t i = 0; i < count && !covered; ++i) {
                covered = compatible(x + samples[i][0], y + 1, z + samples[i][1], kind);
            }
            if (covered) {
                heights[corner] = 255;
                continue;
            }
            uint32_t total = 0;
            uint32_t weight = 0;
            for (size_t i = 0; i < count; ++i) {
                int32_t sx = x + samples[i][0];
                int32_t sz = z + samples[i][1];
                if (std::optional<Cell> cell = liquid(sx, y, sz)) {
                    if (cell->kind != kind) {
                        continue;
                    }
                    uint32_t sampleWeight = cell->height >= 204 ? 10 : 1;
                    total += uint32_t(cell->height) * sampleWeight;
                    weight += sampleWeight;
                } else if (open(sx, y, sz)) {
                    weight += 1;
                }
            }
            heights[corner] = weight == 0 ? 0 : static_cast<uint8_t>((total + weight / 2) / weight);
        }
        return heights;
    }

    std::array<int32_t, 2> flowGradient(int32_t x, int32_t y, int32_t z, const Cell& cell) const
    {
        static constexpr int32_t Offsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        int32_t current = cell.effectiveDepth();
        std::array<int32_t, 2> gradient {};
        for (const auto& offset : Offsets) {
            int32_t ax = x + offset[0];
            int32_t az = z + offset[1];
            std::optional<int32_t> delta;
            if (std::optional<Cell> other = liquid(ax, y, az)) {
                if (other->kind == cell.kind) {
                    delta = int32_t(other->effectiveDepth()) - current;
                }
            } else if (open(ax, y, az)) {
                if (std::optional<Cell> below = liquid(ax, y - 1, az); below && below->kind == cell.kind) {
                    delta = int32_t(below->effectiveDepth()) - current + 8;
                }
            }
            if (delta) {
                gradient[0] += offset[0] * *delta;
                gradient[1] += offset[1] * *delta;
            }
        }
        return gradient;
    }

    void emit(int32_t x, int32_t y, int32_t z, Face face, const std::array<uint8_t, 4>& heights, uint32_t material, const std::array<int32_t, 2>& gradient, bool falling, std::vector<ModelQuadGpu>& out)
    {
        static constexpr float FaceXz[6][4][2] = {
            { { 0, 0 }, { 0, 0 }, { 0, 1 }, { 0, 1 } },
            { { 1, 1 }, { 1, 1 }, { 1, 0 }, { 1, 0 } },
            { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } },
            { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } },
            { { 1, 0 }, { 1, 0 }, { 0, 0 }, { 0, 0 } },
            { { 0, 1 }, { 0, 1 }, { 1, 1 }, { 1, 1 } },
        };
        static constexpr float BaseUv[6][4][2] = {
            { { 0, 0 }, { 0, 0 }, { 1, 0 }, { 1, 0 } },
            { { 0, 0 }, { 0, 0 }, { 1, 0 }, { 1, 0 } },
            { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } },
            { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } },
            { { 0, 0 }, { 0, 0 }, { 1, 0 }, { 1, 0 } },
            { { 0, 0 }, { 0, 0 }, { 1, 0 }, { 1, 0 } },
        };
        static constexpr uint32_t FaceIds[6] = { 3, 4, 1, 2, 5, 6 };
        size_t faceIndex = static_cast<size_t>(face);
        bool side = face != Face::PositiveY && face != Face::NegativeY;
        float angle = 1.5707963f - std::atan2(float(gradient[1]), float(gradient[0]));
        bool rotateFlow = face == Face::PositiveY && (gradient[0] != 0 || gradient[1] != 0);

        ModelQuad quad;
        for (size_t corner = 0; corner < 4; ++corner) {
            float height = float(heights[corner]) / 255.0f;
            quad.positions[corner] = {
                static_cast<int16_t>(FaceXz[faceIndex][corner][0] * 256.0f),
                static_cast<int16_t>(height * 256.0f),
                static_cast<int16_t>(FaceXz[faceIndex][corner][1] * 256.0f),
            };
            float u = BaseUv[faceIndex][corner][0];
            float v = BaseUv[faceIndex][corner][1];
            if (side) {
                v = 1.0f - height;
            }
            if (rotateFlow) {
                float cu = u - 0.5f;
                float cv = v - 0.5f;
                u = cu * std::cos(angle) - cv * std::sin(angle) + 0.5f;
                v = cu * std::sin(angle) + cv * std::cos(angle) + 0.5f;
            }
            quad.uvs[corner] = { static_cast<uint16_t>((u + 1.0f) * 4096.0f), static_cast<uint16_t>((v + 1.0f) * 4096.0f) };
        }
        quad.material = material;
        quad.flags = FaceIds[faceIndex] | QuadTwoSided;
        emitModelQuad(quad, uint32_t(x), uint32_t(y), uint32_t(z), 0, tints.tintWord(material, uint32_t(x), uint32_t(y), uint32_t(z)), field, out);
        if (side && falling) {
            out.back().words[11] |= 1u << 4;
        }
    }

    void meshCell(int32_t x, int32_t y, int32_t z, const Cell& cell, std::vector<ModelQuadGpu>& out)
    {
        std::array<uint8_t, 4> heights = cornerHeights(x, y, z, cell.kind);
        std::array<int32_t, 2> gradient = flowGradient(x, y, z, cell);
        if (!compatible(x, y + 1, z, cell.kind) && !solid(x, y + 1, z)) {
            bool flowing = gradient[0] != 0 || gradient[1] != 0;
            uint32_t material = flowing ? cell.faces[size_t(Face::NegativeX)] : cell.faces[size_t(Face::PositiveY)];
            emit(x, y, z, Face::PositiveY, heights, material, gradient, cell.falling, out);
        }
        static constexpr std::pair<Face, std::array<int32_t, 2>> Sides[] = {
            { Face::NegativeX, { -1, 0 } },
            { Face::PositiveX, { 1, 0 } },
            { Face::NegativeZ, { 0, -1 } },
            { Face::PositiveZ, { 0, 1 } },
        };
        for (const auto& [face, offset] : Sides) {
            int32_t ax = x + offset[0];
            int32_t az = z + offset[1];
            if (compatible(ax, y, az, cell.kind) || solid(ax, y, az)) {
                continue;
            }
            std::array<uint8_t, 4> sideHeights {};
            switch (face) {
            case Face::NegativeX:
                sideHeights = { 0, heights[0], heights[3], 0 };
                break;
            case Face::PositiveX:
                sideHeights = { 0, heights[2], heights[1], 0 };
                break;
            case Face::NegativeZ:
                sideHeights = { 0, heights[1], heights[0], 0 };
                break;
            default:
                sideHeights = { 0, heights[3], heights[2], 0 };
                break;
            }
            emit(x, y, z, face, sideHeights, cell.faces[size_t(face)], gradient, cell.falling, out);
        }
        if (!compatible(x, y - 1, z, cell.kind) && !solid(x, y - 1, z)) {
            emit(x, y, z, Face::NegativeY, { 0, 0, 0, 0 }, cell.faces[size_t(Face::NegativeY)], gradient, cell.falling, out);
        }
    }

    const BlockAssets& assets;
    const IdMapping& ids;
    const MeshInput& input;
    TintSampler& tints;
    const LightField& field;
};

}

std::shared_ptr<const ChunkLighting> updateChunkLighting(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input, std::shared_ptr<const ChunkLighting> previous)
{
    PaletteFacts facts(assets, ids, input.center.get());
    if (facts.isAir()) return {};
    const BlockVisual* uniform = facts.uniformVisual();
    if (uniform && input.center->storages().size() == 1 && uniform->emitsCubeGeometry()
        && (uniform->flags & FlagOccludesFullFace) && !uniform->hasModel()
        && uniform->blockEntity == EntityNone && !uniform->liquid) {
        bool enclosed = true;
        for (const auto& neighbour : input.neighbours) {
            PaletteFacts adjacent(assets, ids, neighbour.get());
            const BlockVisual* visual = adjacent.uniformVisual();
            enclosed &= visual && (visual->flags & FlagOccludesFullFace);
        }
        if (enclosed) return {};
    }
    return LightField(assets, ids, input).update(std::move(previous));
}

size_t meshOutputBound(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input, size_t maxTemplateQuads)
{
    PaletteFacts facts(assets, ids, input.center.get());
    if (facts.isAir()) return sizeof(ChunkMesh);
    size_t modelCells = 0;
    for (uint32_t x = 0; x < Side; ++x)
        for (uint32_t y = 0; y < Side; ++y)
            for (uint32_t z = 0; z < Side; ++z) modelCells += facts.at(x, y, z).hasModel();
    // Six cube faces, at most two model templates and twelve liquid faces per cell.
    // Twice the emitted size covers geometric vector growth in the supported libraries.
    return sizeof(ChunkMesh) + size_t(Side) * Side * Side * (12 * sizeof(PackedQuad) + 24 * sizeof(ModelQuadGpu) + 1)
        + modelCells * maxTemplateQuads * 4 * sizeof(ModelQuadGpu);
}

ChunkMesh meshSubChunk(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input)
{
    ChunkMesh mesh;
    auto cancelled = [&] { return input.cancelled && input.cancelled->load(std::memory_order_relaxed); };
    if (cancelled()) return mesh;
    PaletteFacts facts(assets, ids, input.center.get());
    if (facts.isAir()) {
        return mesh;
    }

    std::vector<PaletteFacts> neighbourFacts;
    neighbourFacts.reserve(6);
    std::array<const PaletteFacts*, 6> neighbours {};
    for (Face face : AllFaces) {
        neighbourFacts.emplace_back(assets, ids, input.neighbours[static_cast<size_t>(face)].get());
    }
    for (size_t i = 0; i < 6; ++i) {
        neighbours[i] = &neighbourFacts[i];
    }

    TintSampler tints(assets, input);
    LightField field(assets, ids, input);
    VisibilityMasks masks(facts);
    for (Face face : AllFaces) {
        Columns columns = exposedColumns(face, facts, masks, *neighbours[static_cast<size_t>(face)]);
        for (uint32_t slice = 0; slice < Side; ++slice) {
            if (cancelled()) return {};
            std::array<uint32_t, Side> rows {};
            for (uint32_t v = 0; v < Side; ++v) {
                for (uint32_t u = 0; u < Side; ++u) {
                    rows[v] |= ((columns[v][u] >> slice) & 1u) << u;
                }
            }
            greedySlice(facts, tints, field, face, slice, rows, mesh.cubes, mesh.translucentCubes);
        }
    }
    meshModels(assets, input, facts, neighbours, tints, field, mesh.models, mesh.translucentModels);
    if (cancelled()) return {};
    LiquidMesher(assets, ids, input, tints, field).mesh(mesh.models, mesh.translucentModels);
    if (cancelled()) return {};
    if (!mesh.empty()) {
        mesh.light.resize(size_t(Side) * Side * Side);
        for (uint32_t x = 0; x < Side; ++x) {
            for (uint32_t y = 0; y < Side; ++y) {
                for (uint32_t z = 0; z < Side; ++z) {
                    mesh.light[linearIndex(x, y, z)] = static_cast<uint8_t>(field.blockAt(int32_t(x), int32_t(y), int32_t(z)) | (field.skyAt(int32_t(x), int32_t(y), int32_t(z)) << 4));
                }
            }
        }
    }
    return mesh;
}

}
