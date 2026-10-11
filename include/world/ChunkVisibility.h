#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace kestrel::world {

struct ChunkVisibility {
    static constexpr uint16_t Solid = 0xffff;
    std::vector<uint16_t> cells;
    std::vector<uint8_t> exits;

    static constexpr uint16_t index(int x, int y, int z) { return uint16_t(x | (y << 4) | (z << 8)); }

    uint16_t region(uint16_t cell) const
    {
        return cells.empty() ? (exits.empty() ? Solid : 0) : cells[cell];
    }

    static std::shared_ptr<const ChunkVisibility> build(const std::bitset<4096>& solid)
    {
        static const auto air = [] {
            auto result = std::make_shared<ChunkVisibility>();
            result->exits.push_back(63);
            return result;
        }();
        static const auto filled = std::make_shared<const ChunkVisibility>();
        if (solid.none()) return air;
        if (solid.all()) return filled;
        auto result = std::make_shared<ChunkVisibility>();
        result->cells.assign(4096, Solid);
        std::array<uint16_t, 4096> queue;
        for (uint16_t seed = 0; seed < 4096; ++seed) {
            if (solid[seed] || result->cells[seed] != Solid) continue;
            uint16_t region = uint16_t(result->exits.size());
            uint8_t faces = 0;
            size_t begin = 0, end = 1;
            queue[0] = seed;
            result->cells[seed] = region;
            while (begin < end) {
                uint16_t cell = queue[begin++];
                int coordinates[] = { cell & 15, (cell >> 4) & 15, cell >> 8 };
                constexpr int steps[] = { 1, 16, 256 };
                for (int axis = 0; axis < 3; ++axis) {
                    for (int side = 0; side < 2; ++side) {
                        if (coordinates[axis] == (side ? 15 : 0)) {
                            faces |= uint8_t(1u << (axis * 2 + side));
                            continue;
                        }
                        uint16_t next = uint16_t(cell + (side ? steps[axis] : -steps[axis]));
                        if (solid[next] || result->cells[next] != Solid) continue;
                        result->cells[next] = region;
                        queue[end++] = next;
                    }
                }
            }
            result->exits.push_back(faces);
        }
        return result;
    }
};

// Regions meet at matching boundary cells, not just at a shared chunk face.
// This keeps sealed rooms hidden even when they cross a sub-chunk boundary.
class ChunkVisibilityGraph {
public:
    using Coordinate = std::array<int32_t, 3>;

    void set(Coordinate coordinate, std::shared_ptr<const ChunkVisibility> volume)
    {
        if (!volume) {
            if (volumes.erase(coordinate)) ++revision;
            return;
        }
        auto [entry, inserted] = volumes.try_emplace(coordinate, volume);
        if (inserted || entry->second != volume) {
            entry->second = std::move(volume);
            ++revision;
        }
    }

    void clear()
    {
        volumes.clear();
        ++revision;
        enabled = false;
    }

    void update(const std::array<double, 3>& camera)
    {
        enabled = false;
        Coordinate center;
        int local[3];
        for (size_t axis = 0; axis < 3; ++axis) {
            double chunk = std::floor(camera[axis] / 16);
            if (!std::isfinite(chunk) || chunk <= std::numeric_limits<int32_t>::min()
                || chunk >= std::numeric_limits<int32_t>::max()) return;
            center[axis] = int32_t(chunk);
            local[axis] = int(std::floor(camera[axis] - chunk * 16));
        }
        auto source = volumes.find(center);
        if (source == volumes.end()) return;
        uint16_t startRegion = source->second->region(ChunkVisibility::index(local[0], local[1], local[2]));
        if (startRegion == ChunkVisibility::Solid) return;
        if (cachedRevision == revision && cachedCenter == center && cachedRegion == startRegion) {
            enabled = cachedEnabled;
            return;
        }
        cachedRevision = revision;
        cachedCenter = center;
        cachedRegion = startRegion;
        cachedEnabled = false;
        enabled = true;

        minimum = maximum = center;
        for (const auto& [coordinate, volume] : volumes) {
            for (size_t axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], coordinate[axis]);
                maximum[axis] = std::max(maximum[axis], coordinate[axis]);
            }
        }
        size_t count = 1;
        for (size_t axis = 0; axis < 3; ++axis) {
            int64_t width = int64_t(maximum[axis]) - minimum[axis] + 3;
            if (width > 32768 || count > 32768 / size_t(width)
                || minimum[axis] == std::numeric_limits<int32_t>::min()
                || maximum[axis] == std::numeric_limits<int32_t>::max()) {
                enabled = false;
                return;
            }
            --minimum[axis];
            ++maximum[axis];
            sizes[axis] = size_t(width);
            count *= sizes[axis];
        }
        nodes.assign(count, nullptr);
        visible.assign(count, false);
        for (const auto& [coordinate, volume] : volumes) nodes[position(coordinate)] = volume.get();
        offsets.resize(count + 1);
        offsets[0] = 0;
        for (size_t node = 0; node < count; ++node) {
            offsets[node + 1] = offsets[node] + (nodes[node] ? nodes[node]->exits.size() : 1);
        }
        visited.assign(offsets.back(), false);
        queue.clear();
        auto enqueue = [&](size_t node, uint16_t region) {
            visible[node] = true;
            if (region == ChunkVisibility::Solid || visited[offsets[node] + region]) return;
            visited[offsets[node] + region] = true;
            queue.push_back({ node, region });
        };
        enqueue(position(center), startRegion);
        size_t boundaryChecks = 0;
        for (size_t next = 0; next < queue.size(); ++next) {
            if (queue.size() > 65536) { enabled = false; return; }
            auto [node, region] = queue[next];
            const ChunkVisibility* from = nodes[node];
            uint8_t faces = from ? from->exits[region] : 63;
            size_t coordinates[] = { node % sizes[0], (node / sizes[0]) % sizes[1], node / (sizes[0] * sizes[1]) };
            size_t steps[] = { 1, sizes[0], sizes[0] * sizes[1] };
            for (size_t face = 0; face < 6; ++face) {
                size_t axis = face / 2, side = face & 1;
                if (!(faces & (1u << face)) || coordinates[axis] == (side ? sizes[axis] - 1 : 0)) continue;
                size_t neighbor = side ? node + steps[axis] : node - steps[axis];
                visible[neighbor] = true;
                const ChunkVisibility* to = nodes[neighbor];
                if (to && to->exits.empty()) continue;
                if ((!from || from->cells.empty()) && (!to || to->cells.empty())) {
                    enqueue(neighbor, 0);
                    continue;
                }
                for (int u = 0; u < 16; ++u) {
                    for (int v = 0; v < 16; ++v) {
                        if (++boundaryChecks > 2 * 1024 * 1024) { enabled = false; return; }
                        int a[] = { u, v, 0 };
                        if (axis == 0) { a[0] = 0; a[1] = u; a[2] = v; }
                        if (axis == 1) { a[0] = u; a[1] = 0; a[2] = v; }
                        a[axis] = side ? 15 : 0;
                        if (from && from->region(ChunkVisibility::index(a[0], a[1], a[2])) != region) continue;
                        a[axis] = side ? 0 : 15;
                        uint16_t target = to ? to->region(ChunkVisibility::index(a[0], a[1], a[2])) : 0;
                        enqueue(neighbor, target);
                    }
                }
            }
        }
        // Models can extend across edges and corners of their owning sub-chunk.
        auto reached = visible;
        for (size_t node = 0; node < count; ++node) {
            if (!reached[node]) continue;
            size_t coordinates[] = { node % sizes[0], (node / sizes[0]) % sizes[1], node / (sizes[0] * sizes[1]) };
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int64_t x = int64_t(coordinates[0]) + dx;
                        int64_t y = int64_t(coordinates[1]) + dy;
                        int64_t z = int64_t(coordinates[2]) + dz;
                        if (x < 0 || y < 0 || z < 0 || x >= int64_t(sizes[0])
                            || y >= int64_t(sizes[1]) || z >= int64_t(sizes[2])) continue;
                        visible[size_t(x) + sizes[0] * (size_t(y) + sizes[1] * size_t(z))] = true;
                    }
                }
            }
        }
        cachedEnabled = true;
    }

    bool contains(Coordinate coordinate) const
    {
        if (!enabled) return true;
        for (size_t axis = 0; axis < 3; ++axis) {
            if (coordinate[axis] < minimum[axis] || coordinate[axis] > maximum[axis]) return true;
        }
        return visible[position(coordinate)];
    }

private:
    size_t position(Coordinate coordinate) const
    {
        return size_t(int64_t(coordinate[0]) - minimum[0]) + sizes[0]
            * (size_t(int64_t(coordinate[1]) - minimum[1]) + sizes[1] * size_t(int64_t(coordinate[2]) - minimum[2]));
    }

    std::map<Coordinate, std::shared_ptr<const ChunkVisibility>> volumes;
    std::vector<const ChunkVisibility*> nodes;
    std::vector<size_t> offsets;
    std::vector<bool> visible, visited;
    std::vector<std::pair<size_t, uint16_t>> queue;
    Coordinate minimum {}, maximum {}, cachedCenter {};
    std::array<size_t, 3> sizes {};
    uint64_t revision = 1, cachedRevision = 0;
    uint16_t cachedRegion = ChunkVisibility::Solid;
    bool enabled = false;
    bool cachedEnabled = false;
};

}
