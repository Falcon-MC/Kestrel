#include "SessionData.h"

#include <chrono>
#include <stdexcept>

namespace kestrel {

void Session::acknowledgeResourceReload(uint64_t serial, const world::BlockAssets* expectedAssets)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (current.resourceReloadSerial != serial || current.assets.get() != expectedAssets || !current.reloadedMeshes) return;
    current.reloadedMeshes.reset();
    publishSnapshotLocked();
}

void Session::setGlobalPacks(std::vector<std::shared_ptr<const world::PackFiles>> packs, uint64_t revision)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (resourceReloadCancelled) resourceReloadCancelled->store(true, std::memory_order_relaxed);
    requestedGlobalPacks = std::move(packs);
    requestedGlobalRevision = revision;
}

void Session::pollGlobalPacks()
{
    uint64_t wanted;
    std::vector<std::shared_ptr<const world::PackFiles>> globals;
    {
        std::lock_guard<std::mutex> guard(mutex);
        wanted = requestedGlobalRevision;
        globals = requestedGlobalPacks;
    }
    if (resourceReloadJob.valid()) {
        if (resourceReloadJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        ResourceReload result;
        try { result = resourceReloadJob.get(); }
        catch (const std::exception& e) { result.error = e.what(); result.revision = wanted; result.generation = resourceGeneration; result.dimension = motionDimension; }
        if (result.generation == resourceGeneration && result.revision == wanted && result.dimension == motionDimension) {
            appliedGlobalRevision = wanted;
            if (result.assets && result.error.empty()) {
                assets = std::move(result.assets);
                assetsKey.clear();
                ids.sequential = assets->sequentialMap();
                hiddenChecked.clear();
                configurePaletteResolver();
                mesher->clear();
                meshGenerations.clear();
                meshes.clear();
                meshQuads = 0;
                std::vector<MeshUpdate> replacement;
                for (auto& update : result.meshes) {
                    if (!world.store().subChunk(update.key)) continue;
                    meshQuads += update.mesh->quadCount();
                    meshes.emplace(update.key, update.mesh);
                    replacement.push_back(std::move(update));
                }
                world.store().markAllDirty();
                publishNearby();
                publishLoaded();
                publishCameraBlocks();
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    pendingUpdates.clear();
                    current.assets = assets;
                    current.packs = std::move(result.packs);
                    current.materials = assets->materials().size();
                    current.textureLayers = assets->textures().layers;
                    current.diagnosticVisuals = assets->diagnosticVisuals();
                    current.globalResourcesRevision = wanted;
                    current.reloadedMeshes = std::make_shared<const std::vector<MeshUpdate>>(std::move(replacement));
                    ++current.resourceReloadSerial;
                    current.resourceReloadError.clear();
                    current.resourceReloading = false;
                    publishSnapshotLocked();
                }
            } else {
                std::lock_guard<std::mutex> guard(mutex);
                current.resourceReloadError = result.error.empty() ? "Resource pack reload failed" : result.error;
                current.resourceReloading = false;
                publishSnapshotLocked();
            }
        }
    }
    if (appliedGlobalRevision == wanted) return;

    std::vector<std::shared_ptr<const world::PackFiles>> stack = sessionServerPacks;
    stack.insert(stack.end(), globals.begin(), globals.end());
    std::map<world::SubChunkKey, std::shared_ptr<const world::SubChunk>> chunks;
    std::map<world::SubChunkKey, std::shared_ptr<const world::PalettedStorage>> biomes;
    std::map<world::SubChunkKey, std::shared_ptr<const world::BlockEntityMap>> entities;
    for (auto& [key, sub] : world.store().allSubChunks()) {
        if (key.dimension != motionDimension) continue;
        chunks.emplace(key, std::move(sub));
        biomes.emplace(key, world.store().biomes(key));
        entities.emplace(key, world.store().blockEntities(key));
    }
    auto mapping = ids;
    auto generation = resourceGeneration;
    auto dimension = motionDimension;
    auto custom = sessionCustomBlocks;
    auto reloadCancelled = std::make_shared<std::atomic_bool>(false);
    {
        std::lock_guard<std::mutex> guard(mutex);
        resourceReloadCancelled = reloadCancelled;
        if (requestedGlobalRevision != wanted || cancelled.load()) reloadCancelled->store(true, std::memory_order_relaxed);
        current.resourceReloading = true;
        current.resourceReloadError.clear();
        publishSnapshotLocked();
    }
    resourceReloadJob = std::async(std::launch::async,
        [stack = std::move(stack), chunks = std::move(chunks), biomes = std::move(biomes), entities = std::move(entities),
            mapping = std::move(mapping), custom = std::move(custom), wanted, generation, dimension, reloadCancelled]() mutable {
        ResourceReload result;
        result.revision = wanted;
        result.generation = generation;
        result.dimension = dimension;
        result.packs = std::move(stack);
        try {
            if (reloadCancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Resource reload cancelled");
            uint64_t archived = 0, expanded = 0;
            for (const auto& pack : result.packs) {
                archived += pack->archiveBytes(); expanded += pack->expandedBytes();
            }
            if (archived > 512ull * 1024 * 1024 || expanded > 1024ull * 1024 * 1024) throw std::runtime_error("Combined resource pack stack exceeds memory budget");
            result.assets = world::BlockAssets::create(result.packs, custom, result.error);
            if (reloadCancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Resource reload cancelled");
            if (result.assets && mapping.sequential && result.assets->sequentialMap()
                && *result.assets->sequentialMap() != *mapping.sequential)
                throw std::runtime_error("Resource packs changed the block palette; reconnect to apply them safely");
            if (!result.assets || !result.error.empty()) return result;
            mapping.sequential = result.assets->sequentialMap();
            auto sub = [&](world::SubChunkKey key) -> std::shared_ptr<const world::SubChunk> {
                auto found = chunks.find(key); return found == chunks.end() ? nullptr : found->second;
            };
            world::DimensionRange range;
            if (!world::vanillaDimensionRange(dimension, range)) range = { -4, 32 };
            constexpr int32_t offsets[6][3] = { {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1} };
            size_t totalBytes = 0;
            for (const auto& [key, center] : chunks) {
                if (reloadCancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Resource reload cancelled");
                world::MeshInput input;
                input.cancelled = reloadCancelled;
                input.center = center;
                input.origin = { key.x * 16, key.y * 16, key.z * 16 };
                input.skyLight = dimension == 0;
                input.blockEntities = entities.at(key);
                for (size_t face = 0; face < 6; ++face) input.neighbours[face] = sub({ dimension, key.x + offsets[face][0], key.y + offsets[face][1], key.z + offsets[face][2] });
                for (int32_t dx = -1; dx <= 1; ++dx) for (int32_t dz = -1; dz <= 1; ++dz) {
                    auto found = biomes.find({ dimension, key.x + dx, key.y, key.z + dz });
                    if (found != biomes.end()) input.biomes[size_t((dz + 1) * 3 + dx + 1)] = found->second;
                    for (int32_t dy = -1; dy <= 1; ++dy) input.around[size_t((dx + 1) * 9 + (dy + 1) * 3 + dz + 1)] = sub({ dimension, key.x + dx, key.y + dy, key.z + dz });
                    for (int32_t sy = key.y + 2; sy < range.baseSubChunkY + range.subChunkCount; ++sy)
                        if (auto above = sub({ dimension, key.x + dx, sy, key.z + dz })) input.above[size_t((dx + 1) * 3 + dz + 1)].push_back(std::move(above));
                }
                auto mesh = world::meshSubChunk(*result.assets, mapping, input);
                const auto& materials = result.assets->materials();
                auto word = [&](uint32_t id) { return materials.at(id < materials.size() ? id : 0).gpuWord(); };
                for (auto* cubes : { &mesh.cubes, &mesh.translucentCubes }) for (auto& quad : *cubes) quad.material = word(quad.material);
                for (auto* models : { &mesh.models, &mesh.translucentModels }) for (auto& quad : *models) quad.words[10] = word(quad.words[10]);
                totalBytes += (mesh.cubes.size() + mesh.translucentCubes.size()) * sizeof(world::PackedQuad)
                    + (mesh.models.size() + mesh.translucentModels.size()) * sizeof(world::ModelQuadGpu) + mesh.light.size();
                if (totalBytes > 256ull * 1024 * 1024) throw std::runtime_error("Resource reload terrain exceeds memory budget");
                if (!mesh.empty()) result.meshes.push_back({ key, std::make_shared<const world::ChunkMesh>(std::move(mesh)) });
            }
        } catch (const std::exception& e) { result.assets.reset(); result.meshes.clear(); result.error = e.what(); }
        return result;
    });
}
}
