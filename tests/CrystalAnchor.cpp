#include "client/RespawnAnchor.h"
#include "client/CrystalMetadata.h"
#include "world/CrystalBeam.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

ItemStack stack(const char* name, const char* block = nullptr)
{
    ItemStack item;
    item.mDefinition = std::make_shared<ItemDefinition>(name, 1, false, Tag {});
    if (block) item.mBlockDefinition = std::make_shared<BlockDefinition>(block, 1, Tag {});
    item.mCount = 1;
    return item;
}
}

int main()
{
    using namespace kestrel;
    std::array<int32_t, 3> target {};
    EntityDataEntry entry;
    entry.mId = 47;
    entry.mFormat = EntityDataFormat::Vector3i;
    entry.mVector3iValue = Vector3i(12, 80, -25);
    applyCrystalBeamTarget(entry, target);
    require(target == std::array<int32_t, 3>{ 12, 80, -25 }, "Crystal target metadata must retain signed block coordinates");
    entry.mId = 53;
    entry.mVector3iValue = Vector3i();
    applyCrystalBeamTarget(entry, target);
    require(target[0] == 12, "Partial metadata updates must preserve the beam target");
    entry.mId = 47;
    entry.mFormat = EntityDataFormat::Int;
    applyCrystalBeamTarget(entry, target);
    require(target[0] == 12, "Wrong metadata formats must not overwrite the beam target");
    entry.mFormat = EntityDataFormat::Vector3i;
    applyCrystalBeamTarget(entry, target);
    require(target == std::array<int32_t, 3>{}, "Zero target metadata must clear the beam");
    const ItemStack empty, glowstone = stack("minecraft:glowstone"), stone = stack("minecraft:stone"), sword = stack("minecraft:iron_sword");
    Tag states = Tag::ofCompound();
    for (int charge = 0; charge <= 4; ++charge) {
        states.putInt("respawn_anchor_charge", charge);
        require(usesRespawnAnchor(&states, glowstone, false), "Glowstone must use anchors at every charge, including full");
        require(usesRespawnAnchor(&states, empty, false) == (charge > 0), "Empty hands must activate only charged anchors");
        require(usesRespawnAnchor(&states, stone, false) == (charge > 0), "Charged anchors must take priority over block placement");
        require(usesRespawnAnchor(&states, sword, false) == (charge > 0), "Non-block items must activate charged anchors");
        require(!usesRespawnAnchor(&states, glowstone, true) && !usesRespawnAnchor(&states, stone, true), "Sneaking with an item must bypass the anchor");
        require(usesRespawnAnchor(&states, empty, true) == (charge > 0), "Sneaking with empty hands must still activate");
    }
    states.putInt("respawn_anchor_charge", 0);
    require(usesRespawnAnchor(&states, stack("custom:charger", "minecraft:glowstone"), false), "Server block definitions must resolve glowstone items");
    require(!usesRespawnAnchor(&states, stack("minecraft:glowstone", "minecraft:stone"), false), "Resolved block definitions must override item names");
    states.putString("respawn_anchor_charge", "4");
    require(!usesRespawnAnchor(&states, stone, false) && !usesRespawnAnchor(nullptr, empty, false), "Malformed or missing charges must not activate");

    int count = 0;
    world::crystalBeamQuads({ 8, 4, 0 }, { 0, 3, 0 }, [&](const world::CrystalBeamQuad& quad, float offset) {
        ++count;
        require(offset == 0 && quad.brightness == std::array<float, 4>{ 0, 0, 1, 1 }, "Beam must fade from black at target to white at crystal");
        require(std::abs(std::hypot(quad.positions[0][1], quad.positions[0][2]) - 0.15) < 1e-5, "Target ring radius must be 0.15 blocks");
        require(std::abs(std::hypot(quad.positions[2][1], quad.positions[2][2]) - 0.75) < 1e-5, "Crystal ring radius must be 0.75 blocks");
        require(std::abs(quad.positions[0][0] + 8) < 1e-5 && quad.positions[2][0] == 0, "Target must use block Y plus one and crystal origin");
        require(quad.uvs[2][1] == 1, "UV must span one cycle along the beam");
    });
    require(count == 8, "Beam must have eight sides");
    count = 0;
    auto finite = [&](const world::CrystalBeamQuad& quad, float) {
        ++count;
        for (const auto& point : quad.positions) for (float value : point) require(std::isfinite(value), "Vertical beams must remain finite");
    };
    world::crystalBeamQuads({ 0, 40, 0 }, { 0, 2, 0 }, finite);
    require(count == 16, "Long beams must split into safely packed segments");
    count = 0;
    world::crystalBeamQuads({ 8, 4, 0 }, { 0, 0, 0 }, finite);
    world::crystalBeamQuads({ 0, 4, 0 }, { 0, 3, 0 }, finite);
    world::crystalBeamQuads({ std::numeric_limits<double>::quiet_NaN(), 4, 0 }, { 0, 3, 0 }, finite);
    world::crystalBeamQuads({ 5000, 4, 0 }, { 0, 3, 0 }, finite);
    require(count == 0, "Cleared, degenerate and invalid targets must emit no beam");
}
