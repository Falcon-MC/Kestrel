#include "world/Mesher.h"

#include <bit>
#include <optional>

namespace kestrel::world {

namespace {

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
            resolved.push_back(&assets.visual(value, ids.hashed, ids.sequential.get()));
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

bool cullsFace(uint8_t source, uint8_t neighbour)
{
    return (neighbour & FlagOccludesFullFace) || ((source & FlagLeafModel) && (neighbour & FlagLeafModel));
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
            uint32_t neighbourLeaves = isNegative(face) ? leafColumn << 1 : leafColumn >> 1;
            uint32_t leafPairs = leafColumn & neighbourLeaves;
            uint32_t faces = geometryColumn & ~neighbourOccluders & ~leafPairs & FullColumn;

            if (faces & boundaryBit) {
                uint32_t slice = isNegative(face) ? 0 : Side - 1;
                auto [sx, sy, sz] = blockCoordinate(face, slice, u, v);
                auto [nx, ny, nz] = neighbourBoundaryCoordinate(face, u, v);
                if (cullsFace(facts.at(sx, sy, sz).flags, neighbour.at(nx, ny, nz).flags)) {
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

void greedySlice(const PaletteFacts& facts, Face face, uint32_t slice, std::array<uint32_t, Side>& rows, std::vector<PackedQuad>& opaque, std::vector<PackedQuad>& translucent)
{
    size_t faceIndex = static_cast<size_t>(face);
    for (uint32_t v = 0; v < Side; ++v) {
        while (rows[v] != 0) {
            uint32_t u = static_cast<uint32_t>(std::countr_zero(rows[v]));
            auto origin = blockCoordinate(face, slice, u, v);
            uint32_t material = facts.at(origin[0], origin[1], origin[2]).faces[faceIndex];

            uint32_t shifted = rows[v] >> u;
            uint32_t binaryWidth = std::min<uint32_t>(static_cast<uint32_t>(std::countr_one(shifted)), Side - u);
            uint32_t width = 1;
            while (width < binaryWidth) {
                auto [x, y, z] = blockCoordinate(face, slice, u + width, v);
                if (facts.at(x, y, z).faces[faceIndex] != material) {
                    break;
                }
                ++width;
            }

            uint32_t span = ((width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u))) << u;
            uint32_t height = 1;
            while (v + height < Side && (rows[v + height] & span) == span) {
                bool same = true;
                for (uint32_t offset = 0; offset < width && same; ++offset) {
                    auto [x, y, z] = blockCoordinate(face, slice, u + offset, v + height);
                    same = facts.at(x, y, z).faces[faceIndex] == material;
                }
                if (!same) {
                    break;
                }
                ++height;
            }

            for (uint32_t row = v; row < v + height; ++row) {
                rows[row] &= ~span;
            }
            bool isTranslucent = facts.at(origin[0], origin[1], origin[2]).flags & FlagTranslucent;
            (isTranslucent ? translucent : opaque).push_back(PackedQuad::make(origin[0], origin[1], origin[2], face, width, height, material));
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

void emitModelQuad(const ModelQuad& quad, uint32_t x, uint32_t y, uint32_t z, uint32_t rotation, std::vector<ModelQuadGpu>& out)
{
    ModelQuadGpu gpu;
    std::array<int16_t, 12> positions {};
    for (size_t corner = 0; corner < 4; ++corner) {
        int32_t px = quad.positions[corner][0] - 128;
        int32_t py = quad.positions[corner][1];
        int32_t pz = quad.positions[corner][2] - 128;
        for (uint32_t i = 0; i < (rotation & 3); ++i) {
            int32_t rotated = -pz;
            pz = px;
            px = rotated;
        }
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
    gpu.words[11] = face ? uint32_t(rotateFace(*face, rotation)) + 1 : 0;
    out.push_back(gpu);
}

void meshModels(const BlockAssets& assets, const PaletteFacts& facts, const std::array<const PaletteFacts*, 6>& neighbours, std::vector<ModelQuadGpu>& opaque, std::vector<ModelQuadGpu>& translucent)
{
    if (const BlockVisual* uniform = facts.uniformVisual(); uniform && !uniform->hasModel()) {
        return;
    }
    ModelContext context(assets, facts, neighbours);
    const std::vector<ModelTemplate>& templates = assets.modelTemplates();
    const std::vector<ModelQuad>& quads = assets.modelQuads();
    for (uint32_t x = 0; x < Side; ++x) {
        for (uint32_t y = 0; y < Side; ++y) {
            for (uint32_t z = 0; z < Side; ++z) {
                const BlockVisual& visual = facts.at(x, y, z);
                if (!visual.hasModel()) {
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
                        emitModelQuad(quad, x, y, z, rotation, (visual.flags & FlagTranslucent) ? translucent : opaque);
                    }
                }
            }
        }
    }
}

}

ChunkMesh meshSubChunk(const BlockAssets& assets, const IdMapping& ids, const MeshInput& input)
{
    ChunkMesh mesh;
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

    VisibilityMasks masks(facts);
    for (Face face : AllFaces) {
        Columns columns = exposedColumns(face, facts, masks, *neighbours[static_cast<size_t>(face)]);
        for (uint32_t slice = 0; slice < Side; ++slice) {
            std::array<uint32_t, Side> rows {};
            for (uint32_t v = 0; v < Side; ++v) {
                for (uint32_t u = 0; u < Side; ++u) {
                    rows[v] |= ((columns[v][u] >> slice) & 1u) << u;
                }
            }
            greedySlice(facts, face, slice, rows, mesh.cubes, mesh.translucentCubes);
        }
    }
    meshModels(assets, facts, neighbours, mesh.models, mesh.translucentModels);
    return mesh;
}

}
