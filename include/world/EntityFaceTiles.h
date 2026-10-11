#pragma once

#include "world/BlockEntityModels.h"

#include <algorithm>
#include <cmath>

namespace kestrel::world {

template <class Material>
std::vector<ModelQuad> tileEntityFace(const ModelQuad& quad, const EntityFace& face, Material material)
{
    if (face.turns || !(face.w > 0) || !(face.h > 0) || face.w > 128 || face.h > 128) {
        ModelQuad copy = quad;
        copy.material = material(face);
        return { copy };
    }
    std::vector<ModelQuad> tiles;
    for (float y = 0; y < face.h; y += 16) {
        for (float x = 0; x < face.w; x += 16) {
            const float w = std::min(16.0f, face.w - x);
            const float h = std::min(16.0f, face.h - y);
            EntityFace region = face;
            region.u += face.flipU ? face.w - x - w : x;
            region.v += face.flipV ? face.h - y - h : y;
            region.w = w;
            region.h = h;
            ModelQuad tile = quad;
            const std::array<std::array<float, 2>, 4> corners { {
                { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h },
            } };
            for (size_t corner = 0; corner < 4; ++corner) {
                const float u = corners[corner][0] / face.w;
                const float v = corners[corner][1] / face.h;
                for (size_t axis = 0; axis < 3; ++axis) {
                    tile.positions[corner][axis] = int16_t(std::lround(float(quad.positions[0][axis])
                        + u * float(quad.positions[1][axis] - quad.positions[0][axis])
                        + v * float(quad.positions[3][axis] - quad.positions[0][axis])));
                }
            }
            tile.material = material(region);
            tiles.push_back(tile);
        }
    }
    return tiles;
}

}
