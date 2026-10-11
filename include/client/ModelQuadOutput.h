#pragma once

#include "world/Mesher.h"

#include <cmath>

namespace kestrel {

class ModelQuadOutput {
public:
    using Positions = std::array<std::array<float, 3>, 4>;

    ModelQuadOutput(std::vector<world::ModelQuadGpu>& out) : packed(&out) { }
    ModelQuadOutput(std::vector<world::HandQuadGpu>& out) : precise(&out) { }

    void push_back(const world::ModelQuadGpu& model, const Positions& corners)
    {
        if (packed) {
            packed->push_back(model);
            return;
        }
        world::HandQuadGpu quad;
        quad.model = model;
        for (size_t corner = 0; corner < 4; ++corner) {
            for (size_t axis = 0; axis < 3; ++axis) {
                float value = corners[corner][axis] / 256.0f;
                quad.positions[corner * 3 + axis] = std::isfinite(value) ? value : 0.0f;
            }
        }
        precise->push_back(quad);
    }

    size_t size() const { return packed ? packed->size() : precise->size(); }
    world::ModelQuadGpu& operator[](size_t index) const { return packed ? (*packed)[index] : (*precise)[index].model; }
    world::ModelQuadGpu& back() const { return (*this)[size() - 1]; }

private:
    std::vector<world::ModelQuadGpu>* packed = nullptr;
    std::vector<world::HandQuadGpu>* precise = nullptr;
};

}
