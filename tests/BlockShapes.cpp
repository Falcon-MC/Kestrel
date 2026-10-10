#include "world/assets/BlockRules.h"
#include "world/BlockModels.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <tuple>

using namespace kestrel::world;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

void checkAnvil(const char* name, const Tag& states, bool alongZ)
{
    const auto shape = rules::blockShape(name, states);
    std::vector<models::ShapePart> parts;
    for (const auto& box : shape.boxes) {
        models::ShapePart part;
        for (size_t axis = 0; axis < 3; ++axis) {
            part.min[axis] = box.min[axis] * 16;
            part.max[axis] = box.max[axis] * 16;
        }
        part.uvs = box.uvs;
        part.hidden = box.hidden;
        part.uvSize = box.uvSize;
        parts.push_back(part);
    }
    const auto quads = models::shape(parts, {}, shape.turns);
    std::array<int16_t, 2> low { std::numeric_limits<int16_t>::max(), std::numeric_limits<int16_t>::max() };
    std::array<int16_t, 2> high { std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::min() };
    for (const auto& quad : quads) {
        for (const auto& point : quad.positions) {
            if (point[1] != 256) continue;
            for (size_t axis = 0; axis < 2; ++axis) {
                low[axis] = std::min(low[axis], point[axis * 2]);
                high[axis] = std::max(high[axis], point[axis * 2]);
            }
        }
    }
    const int xWidth = high[0] - low[0];
    const int zWidth = high[1] - low[1];
    require(xWidth > 0 && zWidth > 0, "Anvil geometry must contain its top face");
    require(alongZ ? zWidth > xWidth : xWidth > zWidth,
        "The anvil's long axis must align with the server collision orientation");
}

void checkStairs()
{
    const char* corners[] = { "none", "inner_left", "inner_right", "outer_left", "outer_right" };
    const models::Materials materials { 1, 2, 3, 4, 5, 6 };
    for (int direction = 0; direction < 4; ++direction) {
        for (bool upside : { false, true }) {
            Tag states = Tag::ofCompound();
            states.put("weirdo_direction", Tag::ofInt(direction));
            states.put("upside_down_bit", Tag::ofByte(upside));
            require((rules::stairVariant(states) >> 3) == 0,
                "Legacy stairs must continue deriving their corners from neighbours");
            for (unsigned corner = 0; corner < 5; ++corner) {
                states.put("minecraft:corner", Tag::ofString(corners[corner]));
                uint32_t variant = rules::stairVariant(states);
                require(((variant & 4) != 0) == upside, "Stairs must preserve their vertical half");
                uint32_t shape = (variant >> 3) & 7;
                require(shape >= 1 && shape <= 5, "Every explicit stair corner must select a model, including none");
                auto quads = models::stair(materials, upside, shape - 1);
                for (int x : { 64, 192 }) {
                    for (int z : { 64, 192 }) {
                        bool high = direction == 0 ? x > 128 : direction == 1 ? x < 128 : direction == 2 ? z > 128 : z < 128;
                        bool right = direction == 0 ? z > 128 : direction == 1 ? z < 128 : direction == 2 ? x < 128 : x > 128;
                        bool expected = corner == 0 ? high : corner == 1 ? high || !right : corner == 2 ? high || right : corner == 3 ? high && !right : high && right;
                        bool covered = false;
                        for (const auto& quad : quads) {
                            uint32_t face = models::faceId(upside ? models::Down : models::Up);
                            if ((quad.flags & QuadFaceMask) != face || quad.positions[0][1] != (upside ? 0 : 256)) continue;
                            int minX = 256, maxX = 0, minZ = 256, maxZ = 0;
                            for (const auto& point : quad.positions) {
                                int px = point[0] - 128, pz = point[2] - 128;
                                for (uint32_t turn = 0; turn < (variant & 3); ++turn) {
                                    int nextX = -pz;
                                    pz = px;
                                    px = nextX;
                                }
                                minX = std::min(minX, px + 128);
                                maxX = std::max(maxX, px + 128);
                                minZ = std::min(minZ, pz + 128);
                                maxZ = std::max(maxZ, pz + 128);
                            }
                            covered |= x > minX && x < maxX && z > minZ && z < maxZ;
                        }
                        require(covered == expected, "Rotated stair geometry must match its facing and left/right corner");
                    }
                }
            }
        }
    }
}

int main()
{
    checkStairs();
    for (bool upper : { false, true }) {
        Tag states = Tag::ofCompound();
        states.put("upper_block_bit", Tag::ofByte(upper));
        states.put("pale_moss_carpet_side_north", Tag::ofString("short"));
        states.put("pale_moss_carpet_side_east", Tag::ofString("tall"));
        const auto moss = rules::blockShape("pale_moss_carpet", states);
        require(moss.boxes.size() == (upper ? 2 : 3), "Upper moss carpet must omit its floor and retain both side panels");
        const auto& shortSide = moss.boxes[upper ? 0 : 1];
        const auto& tallSide = moss.boxes[upper ? 1 : 2];
        require(shortSide.textureVariant == 1 && tallSide.textureVariant == 0,
            "Moss sides must use separate native tip and base textures");
        require(shortSide.offset[2] > 0 && tallSide.offset[0] < 0,
            "Moss panels must be inset from their supporting faces to avoid depth conflicts");
    }
    const auto egg = rules::blockShape("sniffer_egg", Tag::ofCompound());
    require(rules::classify("sniffer_egg") == rules::Family::Model && egg.boxes.size() == 1,
        "Sniffer eggs must not select full-cube geometry");
    require(egg.boxes[0].min == std::array<int16_t, 3> { 1, 0, 2 }
            && egg.boxes[0].max == std::array<int16_t, 3> { 15, 16, 14 },
        "Sniffer eggs must preserve the inset sides and cropped face UVs");
    models::Materials cropMaterials { 1, 2, 3, 4, 5, 6 };
    for (uint32_t growth = 0; growth < 8; ++growth) {
        const auto stem = models::cropStem(7, growth, 0);
        int height = -16;
        for (const auto& quad : stem) for (const auto& point : quad.positions) height = std::max(height, int(point[1]));
        require(height == int((growth + 1) * 32 - 16), "Stem height must follow its growth instead of using a full-height cross");
    }
    for (uint32_t growth = 0; growth < 5; ++growth) {
        const auto lower = models::pitcherCrop(cropMaterials, growth, false);
        const auto upper = models::pitcherCrop(cropMaterials, growth, true);
        require(lower.size() == (growth == 0 ? 6 : 8), "Pitcher crops must keep their pod at every growth stage");
        require(upper.size() == (growth < 3 ? 0 : 2), "Immature pitcher crops must not display floating upper halves");
    }
    for (uint32_t turns = 0; turns < 4; ++turns) {
        const auto ghast = models::driedGhast(cropMaterials, 7, turns);
        require(ghast.size() == 42, "Dried ghasts must contain a reduced body and six grounded tentacles");
        for (const auto& quad : ghast) for (const auto& point : quad.positions) require(point[1] <= 160, "Dried ghasts must not occupy a full cube");
        for (bool head : { false, true }) {
            const auto bed = models::strawBed(7, head, turns);
            for (const auto& quad : bed) for (const auto& point : quad.positions) require(point[1] <= 80, "Straw beds must omit the normal bed frame and legs");
        }
        for (bool attached : { false, true }) for (bool powered : { false, true }) {
            const auto hook = models::tripwireHook(cropMaterials, attached, powered, turns);
            require(hook.size() == (attached ? 23 : 22), "Tripwire hooks must keep their mount, arm and hollow ring in every state");
        }
    }
    for (uint32_t mask = 0; mask < 256; ++mask) {
        const auto wire = models::redstoneWire(7, 8, mask);
        require(!wire.empty(), "Isolated redstone dust must remain visible");
        for (const auto& quad : wire) {
            if (quad.material == 7) for (const auto& point : quad.positions) require(point[1] == 4, "Redstone dust must sit just above its support");
            if (quad.material == 8) {
                require(quad.uvs[0][0] == quad.uvs[1][0] && quad.uvs[0][0] != quad.uvs[3][0],
                    "Climbing dust must turn the native horizontal line texture vertically");
            }
        }
    }
    for (uint32_t mask = 0; mask < 16; ++mask) {
        for (bool attached : { false, true }) for (bool suspended : { false, true }) {
            const auto wire = models::tripwire(7, mask, attached, suspended);
            for (const auto& quad : wire) for (const auto& point : quad.positions) require(point[1] == (suspended ? 56 : 24), "Tripwire must render a horizontal thread at its support height");
        }
    }
    require(rules::classify("bubble_column") == rules::Family::Liquid, "Bubble columns must use water geometry rather than their unused block texture slots");
    BlockVisual strawVisual;
    require(rules::classifyBlockEntity("straw_bed", Tag::ofCompound(), strawVisual)
            && strawVisual.blockEntity == EntityStrawBed,
        "Straw beds must not select the dyed bed entity model");
    models::Materials plantMaterials { 1, 2, 3, 4, 5, 6 };
    require(rules::modelKind("small_dripleaf_block") == rules::ModelKind::SmallDripleaf,
        "Small dripleaves must select their leaf and stem geometry");
    for (uint32_t turns = 0; turns < 4; ++turns) {
        const auto lower = models::smallDripleaf(plantMaterials, false, turns);
        const auto upper = models::smallDripleaf(plantMaterials, true, turns);
        require(lower.size() == 2 && upper.size() == 17,
            "Only the upper dripleaf segment must contain its three edged leaves");
        for (const auto& quad : lower) {
            require(quad.material == plantMaterials[models::South] && (quad.flags & QuadTwoSided),
                "The lower dripleaf must contain only two-sided stem surfaces");
        }
        for (bool wall : { false, true }) {
            const auto fan = models::coralFan(8, wall, turns);
            require(fan.size() == (wall ? 2 : 4), "Floor and wall fans must have distinct leaf arrangements");
            for (const auto& quad : fan) require(quad.flags & QuadTwoSided, "Coral leaves must be visible from either side");
        }
    }
    const auto blossom = models::sporeBlossom(7, 8);
    require(blossom.size() == 5, "Spore blossoms must contain four petals and their ceiling attachment");
    for (const auto& quad : blossom) {
        for (const auto& point : quad.positions) require(point[1] < 256, "Spore petals must hang below their support");
    }
    require(models::sunflower(7, 8, 9, false).size() == 2, "Lower sunflower stems must omit the flower head");
    const auto sunflower = models::sunflower(7, 8, 9, true);
    require(sunflower.size() == 4 && sunflower[2].material != sunflower[3].material,
        "Sunflower heads must keep separate front and back textures");
    models::Materials chorusMaterials { 1, 2, 3, 4, 5, 6 };
    for (uint32_t mask = 0; mask < 64; ++mask) {
        const auto plant = models::chorus(chorusMaterials, mask);
        uint32_t neighbours = 0;
        for (uint32_t side = 0; side < 6; ++side) neighbours += (mask >> side) & 1;
        require(plant.size() == 6 + neighbours * 4,
            "Chorus arms must replace the core's connecting faces without interior end caps");
        for (uint32_t side = 0; side < 6; ++side) {
            const uint32_t axis = side < 2 ? 0 : side < 4 ? 1 : 2;
            int16_t extent = (side & 1) ? 0 : 256;
            for (const auto& quad : plant) {
                for (const auto& point : quad.positions) {
                    extent = (side & 1) ? std::max(extent, point[axis]) : std::min(extent, point[axis]);
                }
            }
            require(extent == ((mask & (1u << side)) ? ((side & 1) ? 256 : 0) : ((side & 1) ? 224 : 32)),
                "Chorus connections must reach only the neighbour faces selected by the mask");
        }
    }
    for (uint32_t count = 0; count < 4; ++count) {
        for (bool dead : { false, true }) {
            const auto pickles = models::seaPickles(7, count, dead);
            require(rules::classify("sea_pickle") == rules::Family::Model
                    && pickles.size() == (count + 1) * (dead ? 7 : 9),
                "Pickle clusters must retain every body and omit live tips only when dead");
            for (const auto& quad : pickles) {
                for (const auto& point : quad.positions) {
                    require(point[0] >= 0 && point[0] <= 256 && point[2] >= 0 && point[2] <= 256
                            && point[1] >= 0 && point[1] < 160,
                        "Pickles must remain small plants inside their block");
                }
                for (const auto& uv : quad.uvs) {
                    require(uv[0] <= 4096 && uv[1] <= 4096, "Pickle UVs must stay in their native texture");
                }
            }
        }
    }
    models::Materials shriekerMaterials { 1, 2, 3, 4, 5, 6 };
    const auto shrieker = models::sculkShrieker(shriekerMaterials, 7);
    require(rules::classify("sculk_shrieker") == rules::Family::Model && shrieker.size() == 11
            && shrieker[models::Up].material == 7,
        "Shriekers must expose their inner floor and separate crown");
    for (size_t index = 6; index < shrieker.size(); ++index) {
        require((shrieker[index].flags & QuadTwoSided) != 0,
            "Transparent shrieker crown faces must remain visible from inside");
    }
    require(rules::classify("sulfur_spike") == rules::Family::Model
            && rules::modelKind("sulfur_spike") == rules::ModelKind::Cross,
        "Sulfur spikes must use their thickness-dependent sprites instead of cube faces");
    const auto cutter = rules::blockShape("stonecutter_block", Tag::ofCompound());
    require(rules::classify("stonecutter_block") == rules::Family::Model && cutter.boxes.size() == 2
            && cutter.boxes[0].max[1] == 9 && cutter.boxes[1].min[1] == 9
            && cutter.boxes[1].min[2] == cutter.boxes[1].max[2]
            && cutter.boxes[0].faceSides[models::West] == models::North,
        "Stonecutters need a nine-pixel base and a separate blade using the saw texture");
    require(rules::blockTint("leaf_litter", models::Up)
            == (uint8_t(TintKind::Foliage) | (uint8_t(FoliageVariant::Dry) << TintVariantShift)),
        "Leaf litter must use the biome's dry foliage tint instead of its grayscale mask");
    for (const auto& [name, oxidation] : {
             std::pair { "copper_golem_statue", 0u }, { "waxed_exposed_copper_golem_statue", 1u },
             { "weathered_copper_golem_statue", 2u }, { "oxidized_copper_golem_statue", 3u } }) {
        require(rules::classify(name) == rules::Family::Deferred,
            "Statues must use their entity geometry instead of simplified block boxes");
        for (const char* direction : { "south", "west", "north", "east" }) {
            Tag states = Tag::ofCompound();
            states.put("minecraft:cardinal_direction", Tag::ofString(direction));
            BlockVisual visual;
            require(rules::classifyBlockEntity(name, states, visual) && visual.blockEntity == EntityCopperGolemStatue
                    && (visual.variant >> 2) == oxidation && (visual.variant & 3) == rules::facingRotation(direction),
                "Statue models must retain their oxidation and server orientation");
        }
    }
    for (const char* name : { "closed_eyeblossom", "open_eyeblossom", "golden_dandelion", "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "pointed_dripstone" }) {
        require(rules::classify(name) == rules::Family::Model && rules::modelKind(name) == rules::ModelKind::Cross,
            "Flowers must not render their transparent sprites on cube faces");
    }
    for (const char* name : { "grass_path", "dirt_path" }) {
        require(rules::classify(name) == rules::Family::Model && rules::modelKind(name) == rules::ModelKind::Farmland,
            "Paths must use their reduced-height model");
    }
    const auto portal = rules::blockShape("end_portal", Tag::ofCompound());
    require(portal.boxes.empty() && portal.planeSide == models::Up && portal.planeHeight == 192,
        "End portals must be horizontal surfaces without vertical starfield faces");
    for (const char* axis : { "x", "z" }) {
        Tag states = Tag::ofCompound();
        states.put("portal_axis", Tag::ofString(axis));
        const auto nether = rules::blockShape("portal", states);
        require(rules::classify("portal") == rules::Family::Model && nether.boxes.size() == 1,
            "Nether portals must use inset surfaces instead of a cube");
        const auto& box = nether.boxes.front();
        const size_t thicknessAxis = axis[0] == 'x' ? 2 : 0;
        require(box.min[thicknessAxis] == 6 && box.max[thicknessAxis] == 10
                && (box.hidden & (1u << models::Up)) && (box.hidden & (1u << models::Down)),
            "Portal surfaces must follow their axis and omit top and bottom faces");
    }
    for (const char* name : { "powered_repeater", "unpowered_repeater" }) {
        for (int32_t delay = 0; delay < 4; ++delay) {
            Tag states = Tag::ofCompound();
            states.put("repeater_delay", Tag::ofInt(delay));
            const auto shape = rules::blockShape(name, states);
            require(rules::classify(name) == rules::Family::Model && shape.boxes.size() == 3
                    && shape.boxes[0].max[1] == 2 && shape.boxes[2].min[2] == 6 + delay * 2,
                "Repeaters must include their thin base and adjustable delay torch");
        }
    }
    for (const char* name : { "powered_comparator", "unpowered_comparator" }) {
        Tag states = Tag::ofCompound();
        states.put("output_subtract_bit", Tag::ofByte(1));
        const auto shape = rules::blockShape(name, states);
        require(rules::classify(name) == rules::Family::Model && shape.boxes.size() == 4
                && shape.boxes[0].max[1] == 2 && std::string(shape.boxes[3].texture) == "redstone_torch_on",
            "Comparators must include three torches and illuminate subtract mode");
    }
    const auto core = rules::blockShape("heavy_core", Tag::ofCompound());
    require(rules::classify("heavy_core") == rules::Family::Model && core.boxes.size() == 1
            && core.boxes[0].min == std::array<int16_t, 3> { 4, 0, 4 }
            && core.boxes[0].max == std::array<int16_t, 3> { 12, 8, 12 },
        "Heavy cores must match their centered half-block collision box");
    for (const char* name : { "lightning_rod", "exposed_lightning_rod", "waxed_oxidized_lightning_rod" }) {
        require(rules::classify(name) == rules::Family::Model, "Lightning rods must not render as cubes");
        for (int32_t facing = 0; facing < 6; ++facing) {
            Tag states = Tag::ofCompound();
            states.put("facing_direction", Tag::ofInt(facing));
            const auto rod = rules::blockShape(name, states);
            require(rod.boxes.size() == 2 && rod.facing >= 0 && rod.facing < 6,
                "Lightning rods must retain their cap, stem and attachment orientation");
            require((rod.boxes[1].hidden & (1u << models::Up)) != 0,
                "Lightning rod stems must not draw an internal cap face");
        }
    }
    Tag frameStates = Tag::ofCompound();
    const auto emptyFrame = rules::blockShape("end_portal_frame", frameStates);
    frameStates.put("end_portal_eye_bit", Tag::ofByte(1));
    const auto filledFrame = rules::blockShape("end_portal_frame", frameStates);
    require(rules::classify("end_portal_frame") == rules::Family::Model && emptyFrame.boxes.size() == 1
            && emptyFrame.boxes[0].max[1] == 13,
        "Empty end portal frames must place their top at thirteen pixels");
    require(filledFrame.boxes.size() == 2 && filledFrame.boxes[1].min[1] == 13
            && filledFrame.boxes[1].max[1] == 16 && filledFrame.boxes[1].uvs.has_value(),
        "Filled end portal frames must include the eye and its texture regions");
    for (const char* name : { "daylight_detector", "daylight_detector_inverted" }) {
        require(rules::classify(name) == rules::Family::Model, "Daylight detectors must not render as full cubes");
        const auto detector = rules::blockShape(name, Tag::ofCompound());
        require(detector.boxes.size() == 1 && detector.boxes[0].min[1] == 0 && detector.boxes[0].max[1] == 6,
            "Daylight detectors must match their six-pixel height");
    }
    require(rules::classify("azalea") == rules::Family::Model, "Azalea must not render as a full cube");
    require(rules::modelKind("flowering_azalea") == rules::ModelKind::Azalea, "Flowering azalea must share the canopy model");
    require(rules::modelKind("black_candle_cake") == rules::ModelKind::CandleCake, "Candle cakes must include their candle");
    const models::Materials materials { 10, 11, 12, 13, 14, 15 };
    for (const auto& [facing, axis] : {
             std::pair { models::Up, 1 }, { models::East, 0 }, { models::South, 2 } }) {
        const auto chain = models::chain(20, 21, facing);
        require(chain.size() == 2, "Chains must contain two narrow crossed planes");
        for (const auto& quad : chain) {
            for (const auto& position : quad.positions) {
                for (int coordinate = 0; coordinate < 3; ++coordinate) {
                    if (coordinate == axis) continue;
                    require(position[coordinate] >= 104 && position[coordinate] <= 152,
                        "Chains must remain centered inside their selection bounds");
                }
            }
            for (const auto& uv : quad.uvs) {
                require(uv[0] <= 768, "Chains must address their three-pixel texture strip");
            }
        }
    }
    require(rules::classify("big_dripleaf") == rules::Family::Model, "Dripleaves must not render as cubes");
    for (uint32_t turns = 0; turns < 4; ++turns) {
        for (uint32_t tilt = 0; tilt < 4; ++tilt) {
            const auto leaf = models::dripleaf(materials, true, tilt, turns);
            require(leaf.size() == 6, "Dripleaf heads must contain a leaf, its edges and a crossed stem");
            int lowestLeaf = 256;
            for (const auto& quad : leaf) {
                require((quad.flags & QuadTwoSided) != 0, "Dripleaf surfaces must remain visible from below");
                for (const auto& position : quad.positions) {
                    if (quad.material == materials[models::South]) lowestLeaf = std::min(lowestLeaf, int(position[1]));
                    if (quad.material == materials[models::North]) {
                        require(position[1] == 0 || position[1] == 240, "Tilting must leave the stem upright");
                    }
                }
            }
            require(tilt < 2 ? lowestLeaf == 240 : lowestLeaf < 240, "Only tilted dripleaf states must lower the canopy");
        }
        const auto stem = models::dripleaf(materials, false, 0, turns);
        require(stem.size() == 2, "Lower dripleaf segments must not contain a canopy");
    }
    require(rules::classify("sculk_sensor") == rules::Family::Model, "Sculk sensors must not render as full cubes");
    require(rules::modelKind("calibrated_sculk_sensor") == rules::ModelKind::SculkSensor, "Calibrated sensors must include their crystal");
    for (uint32_t turns = 0; turns < 4; ++turns) {
        const auto sensor = models::sculkSensor(materials, 20, 21, turns);
        require(sensor.size() == 12, "A calibrated sensor must contain its base, four tendrils and crossed crystal");
        for (const auto& quad : sensor) {
            for (const auto& position : quad.positions) {
                if (quad.material == 20) {
                    require(position[1] >= 128 && position[1] <= 256, "Sensor tendrils must stand on its half-height base");
                } else if (quad.material == 21) {
                    require(position[1] >= 128 && position[1] <= 320, "Calibrated crystals must extend above their tendrils");
                } else {
                    require(position[1] <= 128, "Sensor base textures must not float above their geometry");
                }
            }
            if (quad.material == 20 || quad.material == 21) {
                require((quad.flags & QuadTwoSided) != 0, "Sensor tendrils and crystal must be visible from both sides");
            }
        }
        const auto ordinary = models::sculkSensor(materials, 20, std::nullopt, turns);
        require(ordinary.size() == 10, "Ordinary sensors must omit the calibration crystal");
    }
    const auto azalea = models::azalea(materials);
    require(azalea.size() == 7, "Azalea must contain an open canopy and crossed stem");
    for (const auto& quad : azalea) {
        require(quad.material == materials[models::North] || quad.material == materials[models::Up] || quad.material == materials[models::East],
            "Azalea must not use potted texture slots");
        require((quad.flags & QuadTwoSided) != 0, "Azalea must remain visible through its foliage");
    }
    const auto cake = models::candleCake(materials, 42);
    int cakeTop = 0;
    int candleBottom = std::numeric_limits<int>::max();
    int candleTop = 0;
    for (const auto& quad : cake) {
        for (const auto& position : quad.positions) {
            if (quad.material == 42) {
                candleBottom = std::min(candleBottom, int(position[1]));
                candleTop = std::max(candleTop, int(position[1]));
            } else {
                cakeTop = std::max(cakeTop, int(position[1]));
            }
        }
    }
    require(cakeTop == 128 && candleBottom == cakeTop && candleTop == 240,
        "The candle and wick must sit on the cake without a floating top face");
    require(rules::classify("beacon") == rules::Family::Model, "Beacon must render its inner core and base");
    const auto beacon = rules::blockShape("beacon", Tag::ofCompound());
    require(beacon.boxes.size() == 3, "Beacon must include its shell, base and core");
    const auto shelf = rules::blockShape("acacia_shelf", Tag::ofCompound());
    require(shelf.boxes.size() == 4, "Shelf must contain a recessed back and separate top and bottom");
    for (const auto& box : shelf.boxes) {
        require(box.min[2] >= 11 && box.max[2] <= 16, "Shelf must occupy five pixels of depth");
        require(box.uvSize == 32, "Shelf UVs must address its 32 pixel atlas");
    }
    for (int type = 0; type < 4; ++type) {
        Tag states = Tag::ofCompound();
        states.putByte("powered_bit", 1);
        states.putInt("powered_shelf_type", type);
        const auto powered = rules::blockShape("acacia_shelf", states);
        const auto& uv = (*powered.boxes.back().uvs)[models::North];
        require(uv[1] >= 16 && uv[3] <= 32, "Powered shelf fronts must select their connector region");
    }
    for (const auto& [cardinal, axis, low, high] : {
             std::tuple { "south", 2, 0, 80 }, { "west", 0, 176, 256 },
             { "north", 2, 176, 256 }, { "east", 0, 0, 80 } }) {
        Tag states = Tag::ofCompound();
        states.putString("minecraft:cardinal_direction", cardinal);
        const auto oriented = rules::blockShape("acacia_shelf", states);
        std::vector<models::ShapePart> parts;
        for (const auto& box : oriented.boxes) {
            models::ShapePart part;
            for (size_t coordinate = 0; coordinate < 3; ++coordinate) {
                part.min[coordinate] = box.min[coordinate] * 16;
                part.max[coordinate] = box.max[coordinate] * 16;
            }
            parts.push_back(part);
        }
        const auto quads = models::shape(parts, {}, oriented.turns);
        for (const auto& quad : quads) {
            for (const auto& position : quad.positions) {
                require(position[axis] >= low && position[axis] <= high,
                    "Shelf orientation must occupy the matching wall-side bounds");
            }
        }
    }
    const auto lectern = models::lectern({ 1, 2, 3, 4, 5, 6 }, 0);
    require(lectern.size() == 16, "Lectern must have a base, an open-ended pedestal and a tilted reading board");
    bool slope = false;
    for (const auto& quad : lectern) {
        if (quad.material != 4) continue;
        const int y0 = quad.positions[0][1];
        for (const auto& point : quad.positions) slope |= point[1] != y0;
    }
    require(slope, "The lectern reading surface must slope instead of remaining horizontal");
    models::ShapePart atlasPart;
    atlasPart.min = { 0, 0, 0 };
    atlasPart.max = { 256, 256, 256 };
    atlasPart.uvSize = 32;
    atlasPart.uvs = std::array<std::array<uint16_t, 4>, 6> {};
    atlasPart.uvs->fill({ 3, 4, 19, 20 });
    const auto atlasQuads = models::shape({ atlasPart }, {}, 0);
    for (const auto& quad : atlasQuads) {
        for (const auto& uv : quad.uvs) {
            require((uv[0] == 384 || uv[0] == 2432) && (uv[1] == 512 || uv[1] == 2560),
                "Subpixel atlas coordinates must retain their normalized precision");
        }
    }
    for (const char* name : { "anvil", "chipped_anvil", "damaged_anvil" }) {
        for (const auto& [cardinal, alongZ] : {
                 std::pair { "south", true }, { "west", false }, { "north", true }, { "east", false } }) {
            Tag states = Tag::ofCompound();
            states.putString("minecraft:cardinal_direction", cardinal);
            states.putInt("direction", alongZ ? 1 : 0);
            checkAnvil(name, states, alongZ);
        }
        for (int direction = 0; direction < 4; ++direction) {
            Tag states = Tag::ofCompound();
            states.putInt("direction", direction);
            checkAnvil(name, states, direction % 2 == 0);
        }
    }
}
