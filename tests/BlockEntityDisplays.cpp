#include "client/BlockEntityItems.h"
#include "client/BlockEntityText.h"
#include "world/BlockEntityModels.h"
#include "world/BookAnimation.h"
#include "world/BeaconBeam.h"
#include "world/ConduitState.h"
#include "world/SpawnerDisplay.h"
#include "world/ShulkerLid.h"
#include "world/BlockModels.h"
#include "world/BannerDisplay.h"
#include "world/EntityFaceTiles.h"
#include "world/PistonDisplay.h"
#include "world/EntityDisplayDimensions.h"
#include "world/PottedPlant.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

Tag stack(const char* name, int8_t count)
{
    Tag item = Tag::ofCompound();
    item.putString("Name", name);
    item.putByte("Count", count);
    item.putShort("Damage", 2);
    return item;
}

}

int main()
{
    using namespace kestrel::world;
    require(kestrel::itemHasGlint("minecraft:enchanted_book", Tag {}) && kestrel::itemHasGlint("minecraft:enchanted_golden_apple", Tag {}),
        "Intrinsic enchanted items must retain their foil without an ench tag");
    require(!kestrel::itemHasGlint("minecraft:potion", Tag {}), "Current Bedrock potions must not inherit historical automatic glint");
    Tag enchantedTag = Tag::ofCompound();
    enchantedTag.put("ench", Tag::ofList(Tag::Type::Compound));
    require(kestrel::itemHasGlint("minecraft:diamond_sword", enchantedTag), "A typed ench list must enable foil even when empty");
    Tag customComponents = Tag::ofCompound();
    Tag customGlint = Tag::ofCompound();
    customGlint.putByte("minecraft:glint", 1);
    customComponents.put("components", customGlint);
    require(kestrel::itemComponentHasGlint(customComponents), "Custom items must inherit foil from their registry components");
    Tag flowerPot = Tag::ofCompound();
    Tag plant = Tag::ofCompound();
    plant.putString("name", "minecraft:poppy");
    plant.put("states", Tag::ofCompound());
    flowerPot.put("PlantBlock", plant);
    require(pottedPlantHash(&flowerPot).has_value(), "A flower pot must decode the native plant state");
    plant.putString("name", "minecraft:stone");
    flowerPot.put("PlantBlock", plant);
    require(!pottedPlantHash(&flowerPot), "Unsupported pot blocks must leave the pot empty");
    plant.putString("name", "minecraft:poppy");
    Tag invalidStates = Tag::ofCompound();
    invalidStates.put("nested", Tag::ofCompound());
    plant.put("states", invalidStates);
    flowerPot.put("PlantBlock", plant);
    require(!pottedPlantHash(&flowerPot), "Nested pot states must not reach the block-state hasher");
    flowerPot.remove("PlantBlock");
    require(!pottedPlantHash(&flowerPot), "Removing the plant must restore the empty pot");
    const auto pottedFlower = models::pottedPlant("minecraft:poppy", { 1, 1, 1, 1, 1, 1 }, 0);
    require(pottedFlower.size() == 2, "Potted flowers must keep both crossed, two-sided planes");
    for (const auto& quad : pottedFlower) for (const auto& point : quad.positions) {
        require(point[0] > 0 && point[0] < 256 && point[2] > 0 && point[2] < 256 && point[1] >= 64 && point[1] <= 256,
            "Potted flowers must start at the soil and fit within one block");
    }
    require(models::pottedPlant("minecraft:cactus", { 1, 1, 1, 1, 1, 1 }, 0).size() == 5,
        "Potted cactus must use a solid stem rather than crossed flower planes");
    require(std::is_sorted(std::begin(EntityDisplaySizes), std::end(EntityDisplaySizes),
        [](const EntityDisplaySize& a, const EntityDisplaySize& b) { return a.name < b.name; }),
        "Native entity dimensions must be sorted for display-size lookup");
    const auto pigSize = entityDisplaySize("minecraft:pig");
    const auto spiderSize = entityDisplaySize("minecraft:spider");
    require(pigSize && spiderSize && pigSize->width == pigSize->height && spiderSize->width > spiderSize->height,
        "Trial-spawner previews must distinguish small and wide native collision bounds");
    require(!entityDisplaySize("minecraft:unknown_display_entity"), "Unknown preview entities must retain their bounded fallback size");
    Tag piston = Tag::ofCompound();
    piston.putFloat("Progress", std::numeric_limits<float>::quiet_NaN());
    require(pistonProgress(piston) == 0, "Non-finite piston progress must not reach model positions");
    piston.putFloat("Progress", 1.5f);
    require(pistonProgress(piston) == 1, "Piston progress must remain within one block of travel");
    PistonAnimation extending { 0, 1, 10 };
    PistonAnimation retracting { 1, 0, 10 };
    require(extending.progress(9) == 0 && extending.progress(11) == 1 && retracting.progress(11) == 0,
        "Piston motion must stop at its endpoints rather than repeating");
    require(std::abs(extending.progress(10.05) - 0.5f) < 0.0001f,
        "Piston motion must interpolate across the two server ticks");
    const models::Materials pistonMaterials { 1, 1, 2, 3, 1, 1 };
    static constexpr uint32_t PistonSides[6] = { models::Down, models::Up, models::South, models::North, models::East, models::West };
    for (int facing = 0; facing < 6; ++facing) {
        const auto direction = pistonDirection(facing);
        const auto body = models::pistonBody(pistonMaterials, 4, PistonSides[facing]);
        const auto head = models::pistonHead(pistonMaterials, 3, PistonSides[facing], true);
        int bodyFront = -1000, headFront = -1000;
        for (const auto& quad : body) for (const auto& point : quad.positions) {
            int along = 0;
            for (size_t axis = 0; axis < 3; ++axis) along += (point[axis] - 128) * direction[axis];
            bodyFront = std::max(bodyFront, along);
        }
        for (const auto& quad : head) for (const auto& point : quad.positions) {
            int along = 0;
            for (size_t axis = 0; axis < 3; ++axis) along += (point[axis] - 128) * direction[axis];
            headFront = std::max(headFront, along);
        }
        require(bodyFront == 64 && headFront == 384,
            "Extended pistons must shorten the base and move their head one block in every server direction");
    }
    ModelQuad cloth;
    cloth.positions = { { { 0, 640, 0 }, { 320, 640, 0 }, { 320, 0, 0 }, { 0, 0, 0 } } };
    EntityFace clothFace { 1, 1, 20, 40 };
    std::vector<EntityFace> regions;
    const auto tiles = tileEntityFace(cloth, clothFace, [&](const EntityFace& region) {
        regions.push_back(region);
        return uint32_t(regions.size());
    });
    require(tiles.size() == 6 && regions[0].w == 16 && regions[1].w == 4 && regions[4].h == 8,
        "Banner faces must retain every native pixel instead of shrinking a 20 by 40 pattern into one tile");
    require(tiles[0].positions[1] == tiles[1].positions[0] && tiles[0].positions[3] == tiles[2].positions[0],
        "Adjacent banner texture tiles must meet without geometry seams");
    Tag pot = Tag::ofCompound();
    pot.put("sherds", Tag::ofList(Tag::Type::String, {
        Tag::ofString("minecraft:heart_pottery_sherd"), Tag::ofString("minecraft:archer_pottery_sherd"),
        Tag::ofString("minecraft:skull_pottery_sherd"), Tag::ofString("minecraft:flow_pottery_sherd"),
    }));
    const auto pottery = potPatterns(pot);
    require(pottery[0] == 10 && pottery[1] == 2 && pottery[2] == 19 && pottery[3] == 21,
        "Decorated pot sherds must retain their four distinct side indices");
    pot.put("sherds", Tag::ofList(Tag::Type::String, { Tag::ofString("../../heart_pottery_sherd"), Tag::ofString("minecraft:brick") }));
    require(potPatterns(pot) == std::array<uint8_t, 4> {}, "Unknown pot sherds must use the plain brick side");
    const std::array<float, 3> corner { 15, 16, 15 };
    const auto atRest = potPosition(corner, 0, 0, 0);
    const auto success = potPosition(corner, 0, 2, 0.1);
    const auto failure = potPosition(corner, 0, 1, 0.1);
    require(success != atRest && failure != atRest && success != failure, "Pot success and failure must animate differently");
    require(potPosition(corner, 0, 2, 0.35) == atRest && potPosition(corner, 0, 1, 10) == atRest,
        "Pot interaction animation must stop and must not loop from persisted metadata");
    Tag trial = Tag::ofCompound();
    Tag spawnData = Tag::ofCompound();
    spawnData.putString("TypeId", "minecraft:zombie");
    trial.put("spawn_data", spawnData);
    require(spawnerDisplay(trial, true).identifier == "minecraft:zombie" && spawnerDisplay(trial).identifier.empty(),
        "Trial spawner packets without an id must use their nested TypeId");
    require(kestrel::blockEntityItem(stack("minecraft:diamond", 0)).empty(), "An empty vault display must remain empty");
    auto vault = kestrel::blockEntityItem(stack("minecraft:diamond", 2));
    require(vault.identifier == "minecraft:diamond" && vault.count == 2 && vault.aux == 2,
        "Vault display stacks must preserve their item, count and data value");
    Tag banner = Tag::ofCompound();
    banner.putInt("Base", 7);
    Tag gradient = Tag::ofCompound();
    gradient.putString("Pattern", "gra");
    gradient.putInt("Color", 0);
    Tag circle = Tag::ofCompound();
    circle.putString("Pattern", "mc");
    circle.putInt("Color", 1);
    Tag unknown = Tag::ofCompound();
    unknown.putString("Pattern", "../../bad");
    unknown.putInt("Color", 16);
    banner.put("Patterns", Tag::ofList(Tag::Type::Compound, { gradient, unknown, circle }));
    auto style = bannerDisplay(banner);
    require(style.color == 7 && style.patterns.size() == 2 && style.patterns[0].color == 0 && style.patterns[1].color == 1,
        "Banner layers must retain packet order and dye indices while rejecting invalid patterns");
    banner.put("Patterns", Tag::ofList(Tag::Type::Compound, std::vector<Tag>(100, gradient)));
    require(bannerDisplay(banner).patterns.size() == 16, "Banner rendering must bound the number of network layers");
    const std::array<int32_t, 3> cell { -12, 80, 4 };
    const auto hinge = bannerClothPosition({ 8, 42, 10 }, false, 0, 25, cell);
    require(hinge[0] == 8 && hinge[1] == 42 && hinge[2] == 10, "Banner waving must leave its top hinge attached to the crossbar");
    const auto wave = bannerClothPosition({ 8, 2, 10 }, false, 0, 25, cell);
    const auto repeated = bannerClothPosition({ 8, 2, 10 }, false, 0, 125, cell);
    require(std::abs(wave[2] - repeated[2]) < 0.001f && wave[2] > 10, "Banner animation must repeat without depending on rendering frames");
    const auto shulker = shulkerBoxBoxes();
    const auto lid = buildEntityQuads({ shulker[0] }, 0, [](const auto&) { return 1; });
    static constexpr uint32_t Directions[6] = { models::Down, models::Up, models::North, models::South, models::West, models::East };
    for (uint32_t facing = 0; facing < 6; ++facing) {
        const auto closed = models::orient(lid, Directions[facing]);
        for (size_t q = 0; q < lid.size(); ++q) {
            for (size_t corner = 0; corner < 4; ++corner) {
                const auto& point = lid[q].positions[corner];
                const auto posed = shulkerLidPosition({ point[0] / 16.0f, point[1] / 16.0f, point[2] / 16.0f }, 0, facing);
                for (size_t axis = 0; axis < 3; ++axis) {
                    require(std::abs(posed[axis] * 16 - closed[q].positions[corner][axis]) < 0.001f,
                        "A closed animated Shulker lid must match its baked orientation");
                }
            }
        }
    }
    const auto up = shulkerLidPosition({ 8, 16, 8 }, 1, 1);
    const auto down = shulkerLidPosition({ 8, 16, 8 }, 1, 0);
    require(up[1] == 24 && down[1] == -8, "The Shulker lid must extend half a block along its opening axis");
    const auto halfway = shulkerLidPosition({ 12, 16, 8 }, 0.5f, 1);
    require(halfway[0] < 8 && halfway[2] > 8 && halfway[1] == 20,
        "Shulker opening must lift and rotate the lid together");
    Tag spawner = Tag::ofCompound();
    spawner.putString("EntityIdentifier", "minecraft:pig");
    spawner.putFloat("DisplayEntityHeight", 1.8f);
    spawner.putFloat("DisplayEntityScale", 1.0f);
    spawner.putShort("Delay", 20);
    auto display = spawnerDisplay(spawner);
    require(display.identifier == "minecraft:pig" && spawnerDisplayScale(display) * display.height <= 0.532f,
        "The configured spawner mob must fit inside its cage");
    spawner.putFloat("DisplayEntityHeight", std::numeric_limits<float>::infinity());
    require(std::isfinite(spawnerDisplay(spawner).height), "Non-finite spawner dimensions must use safe defaults");
    const float spin30 = spawnerDisplaySpin(0, 1.0 / 30.0, 20) * 30;
    const float spin144 = spawnerDisplaySpin(0, 1.0 / 144.0, 20) * 144;
    require(std::abs(spin30 - spin144) < 0.001f, "Spawner rotation must not depend on rendering frame rate");
    Tag sign = Tag::ofCompound();
    Tag front = Tag::ofCompound();
    front.putString("Text", "Front\nSecond line");
    front.putInt("SignTextColor", int32_t(0xFFFF0000u));
    front.putByte("IgnoreLighting", 1);
    sign.put("FrontText", front);
    sign.putString("BackText", "bad");
    auto texts = kestrel::signTexts(sign);
    require(texts[0].text == "Front\nSecond line" && texts[0].color == 0xFFFF0000u && texts[0].glow && texts[1].text.empty(),
        "Sign faces must preserve their own text, dye and glowing state");
    front.putString("Text", std::string(4097, 'x'));
    sign.put("FrontText", front);
    require(kestrel::signTexts(sign)[0].text.empty(), "Oversized sign text must not reach the font renderer");
    const auto wall = kestrel::signTextPose("minecraft:oak_wall_sign", 0, 2, false);
    require(wall.normal[2] < -0.99f && wall.center[2] - wall.depth < 15.0f / 16.0f,
        "Wall sign text must sit outside its front board face");
    const auto hanging = kestrel::signTextPose("minecraft:oak_hanging_sign", 4, 2, true);
    require(hanging.normal[0] < -0.99f && hanging.width < wall.width,
        "Hanging sign text must rotate with its smaller board");
    require(conduitFrame("minecraft:prismarine_bricks") && !conduitFrame("minecraft:stone"),
        "Conduit rings must accept only their four frame materials");
    const auto water = [](int, int, int) { return true; };
    require(conduitFrameCount(water, [](int, int, int) { return true; }) == 42,
        "The three intersecting conduit rings must count 42 distinct blocks");
    require(conduitFrameCount(water, [](int, int y, int) { return y == 0; }) == 16,
        "One complete horizontal conduit ring must activate the cage");
    require(conduitFrameCount([](int x, int y, int z) { return x || y || z; }, [](int, int, int) { return true; }) == 0,
        "Missing water at the conduit must deactivate it even with a full ring");
    require(conduitFrameCount(water, [](int x, int y, int z) { return x == 0 && y == 0 && z == 0; }) == 0,
        "Interior blocks must not count toward the conduit frame");
    const auto conduit = activeConduitBoxes();
    for (size_t part = 0; part < conduit.size(); ++part) {
        const auto quads = buildEntityQuads({ conduit[part] }, 0, [](const auto&) { return 1; });
        require(quads.size() == (part >= 3 ? 1 : 6), "Only the conduit eye must use a single billboard face");
        for (const auto& quad : quads) {
            for (const auto& point : quad.positions) {
                for (float tick : { 0.0f, 65.0f, 66.0f, 132.0f }) {
                    const auto posed = conduitPartPosition(part, { point[0] / 16.0f, point[1] / 16.0f, point[2] / 16.0f }, tick, 1, 0.5f);
                    for (float coordinate : posed) require(std::isfinite(coordinate), "Conduit animation must remain finite");
                    require(posed[1] >= -2 && posed[1] <= 18, "Conduit parts must remain around their frame center");
                }
            }
        }
    }
    require(beaconBase("minecraft:netherite_block") && !beaconBase("minecraft:copper_block"),
        "Beacon pyramid must accept only beacon base materials");
    const auto white = beaconSections(80, 320, [](int) { return BeaconColumnBlock {}; });
    require(white.size() == 1 && white[0].bottom == 80 && white[0].top == 320,
        "Beacon beam must cover a clear world column");
    const auto blocked = beaconSections(80, 320, [](int y) { return BeaconColumnBlock { y == 200, {} }; });
    require(blocked.empty(), "An opaque block anywhere above the beacon must hide the entire beam");
    const auto colored = beaconSections(80, 320, [](int y) {
        return BeaconColumnBlock { false, y == 82 ? std::optional<uint32_t>(0xFF0000) : y == 85 ? std::optional<uint32_t>(0x0000FF) : std::nullopt };
    });
    require(colored.size() == 3 && colored[1].bottom == 82 && colored[1].top == 85
        && colored[1].color == 0xFF0000 && colored[2].color == 0x7F007F,
        "Stained glass must split and mix beam colors at its world height");
    require(beaconGlassColor("minecraft:red_stained_glass_pane") == beaconGlassColor("minecraft:red_stained_glass")
        && !beaconGlassColor("minecraft:tinted_glass"), "Only colored glass and panes must tint a beacon");
    Tag data = Tag::ofCompound();
    data.put("Items", Tag::ofList(Tag::Type::Compound, {
        stack("", 0), stack("minecraft:diamond_sword", 1), stack("", 0), stack("minecraft:apple", 1) }));
    const auto items = kestrel::shelfItems(data);
    require(items[0].empty() && items[2].empty(), "Shelf empty positions must not compact the item list");
    require(items[1].identifier == "minecraft:diamond_sword" && items[1].aux == 2,
        "Shelf network item names and damage must reach the renderer");
    data.put("Items", Tag::ofList(Tag::Type::Compound, { stack("minecraft:stone", 0), stack("minecraft:stone", -1) }));
    const auto empty = kestrel::shelfItems(data);
    require(empty[0].empty() && empty[1].empty(), "Zero and negative shelf counts must not produce ghost items");
    data.putString("Items", "bad");
    require(kestrel::shelfItems(data)[0].empty(), "Invalid shelf NBT must produce an empty display");

    const auto boxes = kestrel::world::enchantingBookBoxes();
    require(boxes.size() == 7, "Book requires two covers, a seam, two page blocks and two moving pages");
    for (size_t part = 0; part < boxes.size(); ++part) {
        const auto quads = kestrel::world::buildEntityQuads({ boxes[part] }, 0, [](const auto&) { return 1; });
        require(quads.size() == 6, "Each book part must keep all atlas faces");
        for (const auto& quad : quads) {
            for (const auto& point : quad.positions) {
                for (float open : { 0.0f, 0.5f, 1.0f }) {
                    const auto placed = kestrel::world::bookPartPosition(part,
                        { point[0] / 16.0f, point[1] / 16.0f, point[2] / 16.0f }, 20, open, 0.4f, 1.0f);
                    for (float coordinate : placed) require(std::isfinite(coordinate), "Book pose must remain finite");
                    require(placed[1] > 8.0f && placed[1] < 21.0f, "Book must hover above the table without an inverted hinge");
                    const auto onLectern = bookPartPosition(part, { point[0] / 16.0f, point[1] / 16.0f, point[2] / 16.0f },
                        0, 0, 0, 0, true);
                    require(onLectern[1] > 13 && onLectern[1] < 22, "Lectern book must rest above the sloped reading board");
                }
            }
        }
    }
    kestrel::world::BookAnimation slow;
    kestrel::world::BookAnimation fast;
    for (int frame = 0; frame < 30; ++frame) slow.advance(1.0 / 30.0, 1.0f);
    for (int frame = 0; frame < 144; ++frame) fast.advance(1.0 / 144.0, 1.0f);
    require(std::abs(slow.openness - fast.openness) < 0.001f && std::abs(slow.flip - fast.flip) < 0.001f,
        "Book animation must not change speed with frame rate");
    require(slow.openness == 1.0f, "Nearby player must open the book");
    for (int frame = 0; frame < 30; ++frame) slow.advance(1.0 / 30.0, std::nullopt);
    require(slow.openness == 0.0f, "Book must close when the player leaves");
    return 0;
}
