#include "client/ActorEquipment.h"
#include "client/ActorProperties.h"
#include "render/Renderer.h"
#include "world/EntityAnimation.h"
#include "world/EntityMaterialBlend.h"
#include "world/Geometry.h"
#include "world/ItemGlint.h"
#include "Core/Json/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

double expression(kestrel::world::EntityAnimator& animator, const char* source)
{
    return animator.evaluate(kestrel::world::molang::Script::compile(source));
}

int main()
{
    using namespace kestrel;
    using namespace kestrel::world;
    for (const char* identifier : { "minecraft:xp_orb", "minecraft:fireball", "minecraft:small_fireball", "minecraft:dragon_fireball" }) {
        require(entityModelYaw(identifier, 75.0f) == 0.0f, "South-facing sprites must keep their visible face toward the camera");
    }
    require(entityModelYaw("minecraft:snowball", 75.0f) == 180.0f, "North-facing sprites must retain their authored basis");
    require(entityModelYaw("minecraft:arrow", 75.0f) == 0.0f, "Projectile animations must retain absolute yaw");
    require(entityModelYaw("minecraft:cow", 435.0f) == 75.0f, "Regular actors must turn with their body yaw");
    EntityMaterialBlendLibrary materials;
    auto materialDocument = json::parse(R"({"materials":{"base":{"+states":["Blending"],"blendSrc":"One","blendDst":"One"},"middle:base":{},"overlay:middle":{},"alpha:base":{"blendDst":"OneMinusSrcAlpha"},"opaque:overlay":{"-states":["Blending"]},"reset:base":{"states":[]},"cycle_a:cycle_b":{},"cycle_b:cycle_a":{}}})");
    materials.parse(*materialDocument);
    require(materials.find("overlay") == EntityBlend::Additive, "Entity overlays must inherit additive blending through material parents");
    require(materials.find("alpha") == EntityBlend::Blend, "A child material must override inherited blend factors");
    require(materials.find("opaque") == EntityBlend::Opaque && materials.find("reset") == EntityBlend::Opaque,
        "Removing or replacing states must disable inherited blending");
    require(!materials.find("missing") && !materials.find("cycle_a"), "Missing and cyclic material parents must not recurse indefinitely");
    auto emissiveDocument = json::parse(R"({"materials":{"glow":{"+defines":["USE_EMISSIVE"]},"mob:glow":{},"masked:mob":{"-defines":["USE_EMISSIVE"]}}})");
    materials.parse(*emissiveDocument);
    require(materials.emissive("mob") == true, "Entity materials must inherit the shader that preserves low-alpha emissive pixels");
    require(materials.emissive("masked") == false && !materials.emissive("cycle_a"), "Removed defines and cycles must not enable emissive shading");
    auto multiDocument = json::parse(R"({"materials":{"multi":{"+defines":["USE_MULTITEXTURE"]},"decor:multi":{},"plain:decor":{"-defines":["USE_MULTITEXTURE"]},"replace:decor":{"defines":[]}}})");
    materials.parse(*multiDocument);
    require(materials.multitexture("decor") == true, "Material aliases must inherit layered textures for entity decorations");
    require(materials.multitexture("plain") == false && materials.multitexture("replace") == false,
        "Removed or replaced defines must disable inherited texture layers");
    EntityRenderController materialController;
    materialController.materialChoices = { { EntityMaterial::Dragon, EntityBlend::Opaque, false }, { EntityMaterial::AlphaTest, EntityBlend::Additive, true } };
    require(materialController.selectedMaterial(0).material == EntityMaterial::Dragon && materialController.selectedMaterial(1).blend == EntityBlend::Additive,
        "Material selectors must change shader and blending together");
    require(materialController.selectedMaterial(-1).material == EntityMaterial::Dragon
            && materialController.selectedMaterial(std::numeric_limits<double>::quiet_NaN()).material == EntityMaterial::Dragon
            && materialController.selectedMaterial(1e300).oneSided,
        "Malformed material indices must select a bounded fallback without integer overflow");
    Tag propertyData = Tag::ofCompound();
    propertyData.putString("type", "test:mob");
    Tag propertyList = Tag::ofList(Tag::Type::Compound);
    for (const auto& [name, type] : { std::pair { "test:state", 3 }, { "test:amount", 1 }, { "test:visible", 2 } }) {
        Tag entry = Tag::ofCompound();
        entry.putString("name", name);
        entry.putInt("type", type);
        if (type == 3) entry.put("enum", Tag::ofList(Tag::Type::String, { Tag::ofString("idle"), Tag::ofString("moving") }));
        propertyList.addToList(std::move(entry));
    }
    propertyData.put("properties", propertyList);
    auto propertySchema = ActorPropertySchema::read(propertyData);
    require(propertySchema.has_value(), "Valid server property schemas must retain their wire order");
    EntityProperties properties;
    properties.mIntProperties = { { 0, 1 }, { 2, 0 } };
    properties.mFloatProperties = { { 1, 0.75f } };
    AnimationInput propertyInput;
    propertySchema->apply(properties, propertyInput.properties);
    std::vector<EntityBone> propertyBones(1);
    EntityAnimator propertyAnimator;
    propertyAnimator.update(nullptr, nullptr, propertyBones, propertyInput);
    require(expression(propertyAnimator, "query.property('test:state') == 'moving'") == 1, "Enum properties must compare as Molang strings");
    require(expression(propertyAnimator, "query.property('test:amount')") == 0.75, "Float properties must use their schema index");
    require(expression(propertyAnimator, "query.has_property('test:visible')") == 1, "A false property is still present");
    require(expression(propertyAnimator, "query.has_property('test:unknown')") == 0, "Unknown properties must remain absent");
    auto tempDocument = json::parse(R"({"scripts":{"initialize":["t.state = 'moving';"],"pre_animation":["v.leaked = t.state == 'moving';","t.state = q.property('test:state');","v.selected = t.state == 'moving';"]}})");
    auto tempScripts = readEntityScripts(*tempDocument);
    EntityAnimator tempAnimator;
    tempAnimator.update(tempScripts.get(), nullptr, propertyBones, propertyInput);
    require(expression(tempAnimator, "v.selected") == 1, "Temporary values must survive between lines in one animation script block");
    require(expression(tempAnimator, "v.leaked") == 0, "Temporary values must not leak between initialization and pre-animation blocks");
    tempAnimator.update(tempScripts.get(), nullptr, propertyBones, propertyInput);
    require(expression(tempAnimator, "v.leaked") == 0, "Temporary values must be reset before the next pre-animation block");
    properties.mIntProperties = { { 0, 0 } };
    properties.mFloatProperties.clear();
    propertySchema->apply(properties, propertyInput.properties);
    require(propertyInput.properties.at("test:amount") == 0.75, "Partial property updates must preserve unchanged values");
    require(propertyInput.properties.at("test:state") == molang::internString("idle"), "An enum update must replace its previous value");
    properties.mIntProperties = { { -1, 1 }, { 0, 99 }, { 1, 9 }, { 2, 2 }, { 999, 0 } };
    properties.mFloatProperties = { { 1, std::numeric_limits<float>::infinity() }, { 0, 1.0f } };
    propertySchema->apply(properties, propertyInput.properties);
    require(propertyInput.properties.at("test:state") == molang::internString("idle") && propertyInput.properties.at("test:amount") == 0.75 && propertyInput.properties.at("test:visible") == 0,
        "Invalid indices, types and values must not corrupt actor properties");
    require(!ActorPropertySchema::read(Tag::ofInt(0)), "Non-compound property schemas must be rejected");
    Tag invalidData = propertyData;
    invalidData.get("properties")->addToList(propertyList.getList().front());
    require(!ActorPropertySchema::read(invalidData), "Duplicate property names must not shift wire indices");
    std::vector<EntityBone> combinedBones(3);
    combinedBones[0].name = "arm";
    combinedBones[0].pivot = { 0, 6, 0 };
    combinedBones[0].parent = 1;
    combinedBones[1].name = "body";
    combinedBones[2].name = "overlay";
    combinedBones[2].parent = 1;
    std::vector<EntityBone> adultBones(2);
    adultBones[0].name = "Body";
    adultBones[0].pivot = { 0, 20, 0 };
    adultBones[1].name = "Arm";
    adultBones[1].parent = 0;
    adultBones[1].pivot = { 0, 22, 0 };
    auto adultPose = poseBonesForGeometry(combinedBones, adultBones);
    require(adultPose[0].pivot[1] == 22 && adultPose[0].parent == 1 && adultPose[1].parent == -1,
        "Selected adult geometry must replace baby pivots and remap parent indices");
    require(adultPose[2].name == "overlay" && adultPose[2].parent == 1,
        "Selecting a geometry must preserve additional controller bones");
    auto babyBones = adultBones;
    babyBones[1].pivot[1] = 6;
    auto babyPose = poseBonesForGeometry(combinedBones, babyBones);
    require(babyPose[0].pivot[1] == 6, "Switching variants must restore the selected geometry's pivot");
    auto absoluteDescription = json::parse(R"({"animations":{"move":"animation.guardian.spikes"},"scripts":{"animate":["move"]}})");
    auto absoluteScripts = readEntityScripts(*absoluteDescription);
    auto absoluteDocument = json::parse(R"({"animations":{"animation.guardian.spikes":{"loop":true,"bones":{"spike":{"position":["-this","16-this","-this"]}}}}})");
    AnimationLibrary absoluteLibrary;
    absoluteLibrary.parse(*absoluteDocument);
    std::vector<EntityBone> absoluteBones(1);
    absoluteBones[0].name = "spike";
    absoluteBones[0].pivot = { 0, 24, 0 };
    AnimationInput absoluteInput;
    absoluteInput.identifier = "minecraft:guardian";
    EntityAnimator absoluteAnimator;
    absoluteAnimator.update(absoluteScripts.get(), &absoluteLibrary, absoluteBones, absoluteInput);
    require(absoluteAnimator.matrices()[0][7] == -8, "Absolute guardian positions must subtract their geometry pivot");
    absoluteInput.identifier = "minecraft:elder_guardian";
    absoluteAnimator.update(absoluteScripts.get(), &absoluteLibrary, absoluteBones, absoluteInput);
    require(absoluteAnimator.matrices()[0][7] == -8, "Elder guardians must use the same absolute spike coordinates");
    auto polarDescription = json::parse(R"({"animations":{"move":"animation.polarbear.move"},"scripts":{"animate":["move"]}})");
    auto polarScripts = readEntityScripts(*polarDescription);
    auto polarDocument = json::parse(R"({"animations":{"animation.polarbear.move":{"loop":true,"bones":{"body":{"position":[0,"-9-this",0]},"leg":{"position":[0,0,0]}}}}})");
    AnimationLibrary polarLibrary;
    polarLibrary.parse(*polarDocument);
    std::vector<EntityBone> polarBones(2);
    polarBones[0].name = "body";
    polarBones[0].pivot = { 0, 15, 0 };
    polarBones[1].name = "leg";
    polarBones[1].pivot = { 0, 10, 0 };
    polarBones[1].parent = 0;
    absoluteInput.identifier = "minecraft:polar_bear";
    absoluteAnimator.update(polarScripts.get(), &polarLibrary, polarBones, absoluteInput);
    require(absoluteAnimator.matrices()[0][7] == 0 && absoluteAnimator.matrices()[1][7] == 0,
        "Legacy polar bear body coordinates must not translate its legs through the floor");
    auto endermanDescription = json::parse(R"({"animations":{"pose":"animation.enderman.base_pose"},"scripts":{"animate":["pose"]}})");
    auto endermanScripts = readEntityScripts(*endermanDescription);
    auto endermanDocument = json::parse(R"({"animations":{"animation.enderman.base_pose":{"loop":true,"bones":{"body":{"position":[0,"11-this",0]},"head":{"position":[0,"-this",0]},"hat":{"position":[0,"-this",0]},"leg":{"position":[0,"-5-this",0]}}}}})");
    AnimationLibrary endermanLibrary;
    endermanLibrary.parse(*endermanDocument);
    std::vector<EntityBone> endermanBones(4);
    for (size_t index = 0; index < endermanBones.size(); ++index) {
        endermanBones[index].name = std::array { "body", "head", "hat", "leg" }[index];
        endermanBones[index].pivot[1] = std::array { 38.0f, 24.0f, 38.0f, 26.0f }[index];
        endermanBones[index].parent = std::array { -1, 0, 1, 0 }[index];
    }
    absoluteInput.identifier = "minecraft:enderman";
    absoluteAnimator.update(endermanScripts.get(), &endermanLibrary, endermanBones, absoluteInput);
    const auto& endermanMatrices = absoluteAnimator.matrices();
    require(endermanMatrices[0][7] == -3 && endermanMatrices[1][7] == 11 && endermanMatrices[2][7] == -3 && endermanMatrices[3][7] == 4,
        "Legacy Enderman positions must use each parent origin rather than stacking absolute offsets");
    static_assert(sizeof(ActorDraw::constants) == 272);
    ModelQuadGpu glintQuad;
    glintQuad.words[10] = 8191;
    glintQuad.words[14] = 0xa0123456u;
    applyItemGlint(glintQuad, 8191, 1.0, 100.0f, 100.0f);
    require((glintQuad.words[10] & 0x1fffu) == 8191, "Glint metadata must preserve the base texture layer");
    require(glintQuad.glint[3] == 8191.0f, "Glint must address all entity texture pages");
    require(glintQuad.words[14] == 0xa0123456u, "Glint must preserve leather dye and cutout mode");
    auto phase = itemGlintParameters(1.0, 1.0f, 100.0f);
    require(std::abs(phase[0] + 4.0f / 7.0f) < 1e-6f && std::abs(phase[1] - 1.0f / 3.0f) < 1e-6f,
        "Foil layers must use independent 1750 ms and 3000 ms clocks");
    require(phase[2] == 0.01f, "Low foil strength must not disappear through five-bit quantization");
    auto advanced = itemGlintParameters(1.01, 100.0f, 100.0f);
    require(advanced[0] != phase[0] && advanced[1] != phase[1], "Foil must advance within the old 125 ms frame");
    require(itemGlintParameters(2.0, 1.0f, 50.0f) == phase, "Half speed must preserve the two relative layer periods");
    require(itemGlintParameters(22.0, 1.0f, 100.0f) == phase, "The combined foil animation repeats after 21 seconds");
    auto stopped = itemGlintParameters(1234.0, 100.0f, 0.0f);
    require(stopped[0] == 0.0f && stopped[1] == 0.0f, "Zero speed must stop both layers");
    auto uv = itemGlintUv(0.5f, 0.5f, stopped);
    require(uv == std::array<float, 4> { 0.25f, 0.25f, 0.25f, 0.25f }, "Foil rotation must be centered before the half UV scale");
    auto right = itemGlintUv(1.0f, 0.5f, stopped);
    require(std::abs(right[0] - 0.48492315f) < 1e-6f && std::abs(right[1] - 0.33550504f) < 1e-6f
        && std::abs(right[2] - 0.29341204f) < 1e-6f && std::abs(right[3] - 0.00379806f) < 1e-6f,
        "Foil must use real minus-20 and plus-80 degree rotations instead of shears");
    ModelQuadGpu animated;
    animated.words[10] = 0xdedbe123u;
    animated.words[14] = 0x12345678u;
    animated.words[15] = 0x3c003c00u;
    auto baseWords = animated.words;
    applyItemGlint(animated, 42, 1.0, 50.0f, 100.0f, { 0.5f, 0.5f, 300.0f, 200.0f });
    require(animated.words == baseWords && animated.glint[3] == 42 && animated.glintTexture[2] == 300,
        "Foil must preserve animated base materials and dye, including HD texture dimensions");
    ModelQuadGpu unchanged;
    applyItemGlint(unchanged, NoEntityChoice, 1.0, 100.0f, 100.0f);
    require(unchanged.words[10] == 0, "Missing glint textures must leave the base material intact");
    applyItemGlint(unchanged, 1, 1.0, 0.0f, 100.0f);
    require(unchanged.words[10] == 0, "Disabling glint must preserve the base material");
    auto foilMaterials = json::parse(R"({"materials":{"foil":{"+defines":["GLINT"]},"inherit:foil":{},"remove:inherit":{"-defines":["GLINT"]},"replace:inherit":{"defines":[]}}})");
    materials.parse(*foilMaterials);
    require(materials.glint("inherit") == true && materials.glint("remove") == false && materials.glint("replace") == false,
        "Foil material inheritance must honor added, removed and replaced defines");
    static_assert(actorEquipmentIsOffhand("minecraft:armor_stand", 0, 1));
    static_assert(!actorEquipmentIsOffhand("minecraft:armor_stand", 0, 0));
    static_assert(!actorEquipmentIsOffhand("minecraft:player", 0, 1));
    static_assert(actorEquipmentIsOffhand("minecraft:player", 119, 0));
    static_assert(!actorEquipmentIsOffhand("minecraft:armor_stand", 120, 1));
    auto legacyDescription = json::parse(R"({"animations":{"walk":"animation.test.walk","move":"animation.test.move"},"animation_controllers":[{"move":"controller.animation.test.move"}]})");
    auto legacyScripts = readEntityScripts(*legacyDescription);
    auto legacyDocument = json::parse(R"({"animations":{"animation.test.walk":{"loop":true,"bones":{"leg":{"rotation":[30,0,0]}}},"animation.test.move":{"loop":true,"bones":{"leg":{"rotation":[10,0,0]}}}},"animation_controllers":{"controller.animation.test.move":{"initial_state":"default","states":{"default":{"animations":["walk","move"]}}}}})");
    AnimationLibrary legacyLibrary;
    legacyLibrary.parse(*legacyDocument);
    std::vector<EntityBone> legacyBones(1);
    legacyBones[0].name = "leg";
    EntityAnimator legacyAnimator;
    legacyAnimator.update(legacyScripts.get(), &legacyLibrary, legacyBones, AnimationInput {});
    require(std::abs(legacyAnimator.matrices()[0][5] - std::cos(40.0 * 3.14159265359 / 180.0)) < 1e-6,
        "A legacy controller must play both clips once when its alias matches a clip alias");
    GeometryLibrary geometries;
    geometries.parse(R"({"geometry.test":{"bones":[{"name":"body","bind_pose_rotation":[90,0,0]}]}})");
    geometries.parse(R"({"geometry.test":{"bones":[{"name":"body"}]}})", true);
    require(geometries.find("geometry.test")->bones[0].bindRotation[0] == 90.0f, "Version layers must retain an omitted native bind pose");
    geometries.parse(R"({"geometry.test":{"bones":[{"name":"body","bind_pose_rotation":[0,0,0]}]}})");
    require(geometries.find("geometry.test")->bones[0].bindRotation[0] == 0.0f, "An explicit bind pose must override an earlier layer");
    geometries.parse(R"({"geometry.test":{"bones":[{"name":"body","bind_pose_rotation":[90,0,0]}]}})");
    geometries.parse(R"({"geometry.test":{"bones":[{"name":"body"}]}})");
    require(!geometries.find("geometry.test")->bones[0].bindRotationSet, "Custom packs must override native bind poses");
    geometries.parse(R"({"geometry.dragon":{"bones":[{"name":"neck"}]}})");
    geometries.expandVanillaModels();
    require(geometries.find("geometry.dragon")->bones.size() == 1, "Do not expand custom dragon geometries");

    std::vector<EntityBone> bones(1);
    bones[0].name = "root";
    EntityAnimator animator;
    auto axisDocument = json::parse(R"({"scripts":{"scaleX":2,"scaleY":3,"scaleZ":4}})");
    auto axisScripts = readEntityScripts(*axisDocument);
    animator.update(axisScripts.get(), nullptr, bones, AnimationInput {});
    require(animator.matrices()[0][0] == 2 && animator.matrices()[0][5] == 3 && animator.matrices()[0][10] == 4,
        "Entity scripts must apply independent model scale axes");
    animator.update(nullptr, nullptr, bones, AnimationInput {});
    require(animator.matrices()[0][0] == 1 && animator.matrices()[0][5] == 1, "Model axis scales must not leak between updates");
    auto inherited = molang::Script::compile("this * 0.5");
    require(animator.evaluateWithThis(inherited, 1.0) == 0.5, "Render colors must supply their inherited channel as this");
    require(animator.evaluateWithThis(inherited, 0.25) == 0.125, "The inherited color must be evaluated independently for every channel");
    AnimationInput input;
    EntityAnimator crystalBase;
    input.flags[0] = uint64_t(1) << 38;
    crystalBase.update(nullptr, nullptr, bones, input);
    require(expression(crystalBase, "query.show_bottom") == 1, "Crystal base must follow server flag 38");
    input.flags[0] = 0;
    crystalBase.update(nullptr, nullptr, bones, input);
    require(expression(crystalBase, "query.show_bottom") == 0, "Crystal base must hide when the server clears the flag");
    input.mainHandItem = "minecraft:iron_sword";
    input.offHandItem = "minecraft:shield";
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.is_item_name_any('slot.weapon.offhand', 'minecraft:shield')") == 1.0, "Shield animation must read the offhand item");
    require(expression(animator, "query.is_item_name_any('slot.weapon.mainhand', 0, 'minecraft:iron_sword')") == 1.0, "Item matching must accept the native slot index argument");
    require(expression(animator, "query.is_item_name_any('slot.weapon.mainhand', 'minecraft:shield')") == 0.0, "Item matching must keep both hands separate");
    require(expression(animator, "query.is_item_equipped") == 1.0, "Equipment query defaults to the main hand");
    require(expression(animator, "query.is_item_equipped('off_hand')") == 1.0, "Offhand equipment must select its animation controller state");
    require(expression(animator, "query.is_item_equipped('invalid')") == 0.0, "Unknown hand names must not report equipment");
    require(expression(animator, "query.is_item_equipped(0)") == 1.0 && expression(animator, "query.is_item_equipped(1)") == 1.0, "Equipment queries must accept numeric hand slots");
    input.offHandItem.clear();
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.is_item_equipped('off_hand')") == 0.0, "Removing offhand equipment must clear the query");
    input.identifier = "minecraft:wolf";
    input.now = 1.0;
    animator.update(nullptr, nullptr, bones, input);
    require(std::abs(expression(animator, "query.tail_angle") - 0.6283185482) < 1e-6, "Wild wolf tail must use the native radian angle");
    input.flags[0] = uint64_t(1) << 25;
    input.now += 0.05;
    animator.update(nullptr, nullptr, bones, input);
    require(std::abs(expression(animator, "query.tail_angle") - 1.5393804312) < 1e-6, "Angry wolf tail must override its health angle");
    input.flags[0] = uint64_t(1) << 28;
    input.health = 10;
    animator.update(nullptr, nullptr, bones, input);
    require(std::abs(expression(animator, "query.tail_angle") - 0.35 * 3.14159265359) < 1e-6, "Tamed wolf tail must follow its health fraction");
    input.deathTicks = 20;
    input.health = 0;
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.is_alive") == 0, "A dying actor must not animate as alive");
    require(std::abs(animator.matrices()[0][0]) < 1e-6 && std::abs(animator.matrices()[0][4] - 1.0f) < 1e-6, "Generic death tilt must reach a quarter turn");
    input = AnimationInput {};
    input.identifier = "minecraft:horse";
    input.armorItems[1] = "minecraft:leather_horse_armor";
    input.armorColors[1] = 0x123456;
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.armor_texture_slot(1)") == 1, "Horse armor must select its authored leather texture");
    require(std::abs(expression(animator, "query.armor_color_slot(1, 0)") - 18.0 / 255) < 1e-6, "Authored armor colors must read the equipped stack dye");
    input.armorItems[4] = "minecraft:diamond_horse_armor";
    input.armorColors[4] = 0x123456;
    input.armorDamage[4] = 20;
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.armor_texture_slot(4)") == 4, "Body armor must select the native horse armor layer");
    require(expression(animator, "query.is_item_name_any('slot.armor.body', 'minecraft:diamond_horse_armor')") == 1, "Body attachables must see their equipped item");
    require(std::abs(expression(animator, "query.armor_color_slot(4, 2)") - 86.0 / 255) < 1e-6, "Body armor must preserve its dye channels");
    require(expression(animator, "query.armor_damage_slot(4)") == 20, "Wolf armor cracks must read the body item's damage");
    input.armorItems[4].clear();
    input.armorDamage[4] = 0;
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.is_item_name_any('slot.armor.body', 'minecraft:diamond_horse_armor')") == 0, "Removing body armor must hide the attachable");
    require(expression(animator, "query.armor_texture_slot(5)") == 0, "Out-of-range armor slots must remain invalid");

    input.identifier = "minecraft:llama";
    for (const auto& [item, index] : { std::pair { "minecraft:white_carpet", 1 }, { "minecraft:light_gray_carpet", 9 },
             { "minecraft:red_carpet", 15 }, { "minecraft:black_carpet", 16 }, { "minecraft:diamond_horse_armor", 0 }, { "", 0 } }) {
        input.armorItems[4] = item;
        animator.update(nullptr, nullptr, bones, input);
        require(expression(animator, "v.DecorTextureIndex") == index, "Llama decorations must follow equipped carpet colors and clear on removal");
    }

    input.identifier = "minecraft:wolf";
    input.armorItems[4] = "minecraft:wolf_armor";
    input.armorColors[4] = 0x123456u;
    input.armorDamage[4] = 20;
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.armor_damage_slot(1)") == 20, "Legacy wolf armor queries must address the body equipment");
    require(std::abs(expression(animator, "query.armor_color_slot(1, 0)") - 18.0 / 255.0) < 1e-6, "Wolf armor dye must come from body equipment");

    auto wolfDescription = json::parse(R"({"animations":{"audit_setup":"animation.wolf.setup"},"scripts":{"animate":["audit_setup"]}})");
    auto wolfScripts = readEntityScripts(*wolfDescription);
    auto wolfDocument = json::parse(R"({"animations":{"animation.wolf.setup":{"loop":true,"bones":{"root":{"position":["3-this","-12-this","7-this"],"rotation":["45-this",0,0]}}}}})");
    AnimationLibrary wolfLibrary;
    wolfLibrary.parse(*wolfDocument);
    std::vector<EntityBone> wolfBones(1);
    wolfBones[0].name = "root";
    wolfBones[0].pivot = { -3, 12, 7 };
    wolfBones[0].rotation = { -45, 0, 0 };
    EntityAnimator absoluteWolf;
    absoluteWolf.update(wolfScripts.get(), &wolfLibrary, wolfBones, input);
    const BoneMatrix& wolfMatrix = absoluteWolf.matrices()[0];
    require(std::abs(wolfMatrix[3]) < 1e-6, "Absolute wolf setup must preserve the horizontal bone origin");
    require(std::abs(wolfMatrix[5] - std::cos(45.0 * 3.14159265359 / 180.0)) < 1e-6, "Wolf setup must not apply its bind rotation twice");
    require(std::abs(wolfMatrix[7] + wolfMatrix[4] * -3 + wolfMatrix[5] * 12 + wolfMatrix[6] * 7 - 12) < 1e-5, "Wolf setup must leave its body above the feet");

    EntityAnimator walking;
    input = AnimationInput {};
    input.now = 1.0;
    walking.update(nullptr, nullptr, bones, input);
    input.now = 1.051;
    input.x = 0.1;
    walking.update(nullptr, nullptr, bones, input);
    walking.setRenderContext(input, input.now, 1.0f);
    require(std::abs(expression(walking, "query.modified_move_speed") - 0.16) < 1e-6, "Native walk speed must retain the tick acceleration");
    require(std::abs(expression(walking, "query.walk_distance") - 0.18) < 1e-6, "Stride queries must use the native read scale");
    input.flags[0] = uint64_t(1) << 2;
    input.now = 1.102;
    walking.update(nullptr, nullptr, bones, input);
    walking.setRenderContext(input, input.now, 1.0f);
    require(expression(walking, "query.modified_move_speed") == 0, "Riding must stop the walk cycle");

    input.walkDistance = 0.75;
    input.now = 1.153;
    walking.update(nullptr, nullptr, bones, input);
    require(expression(walking, "query.walk_distance") == 0.75, "First-person animations must use the supplied native walking phase");
    require(expression(walking, "query.distance_moved") == 0.75, "Walking phase aliases must agree");
    input.walkDistance = 0.0;
    input.now = 1.204;
    walking.update(nullptr, nullptr, bones, input);
    require(expression(walking, "query.walk_distance") == 0.0, "A reset walking phase must override the accumulated animation distance");

    EntityAnimator fish;
    input = AnimationInput {};
    input.identifier = "minecraft:tropicalfish";
    input.variant = 257;
    input.markVariant = 5;
    input.nativeVelocity = { 0.0f, 0.0f, 2.0f };
    input.now = 1.0;
    fish.update(nullptr, nullptr, bones, input);
    require(expression(fish, "variable.tropicalfish.base") == 1 && expression(fish, "variable.tropicalfish.pattern") == 11, "Tropical fish selectors must combine family and pattern");
    input.now = 1.051;
    fish.update(nullptr, nullptr, bones, input);
    require(std::abs(expression(fish, "variable.animationamount") - 1.2) < 1e-6, "Fish phase must use server velocity per tick");
    fish.setRenderContext(input, 1.075, 0.5f);
    require(expression(fish, "query.frame_alpha") == 0.5, "Render queries must observe the current partial tick");
    require(std::abs(expression(fish, "variable.animationamount") - 1.2) < 1e-6, "Refreshing render queries must not advance native phases");

    EntityAnimator inverted;
    input = AnimationInput {};
    input.identifier = "minecraft:cow";
    input.metadataQueries["upside_down_height"] = 20.8;
    for (const char* name : { "Dinnerbone", "Grumm" }) {
        input.name = name;
        inverted.update(nullptr, nullptr, bones, input);
        require(inverted.matrices()[0][0] == -1 && inverted.matrices()[0][5] == -1, "Special mob names must invert the model");
        require(std::abs(inverted.matrices()[0][7] - 20.8f) < 1e-5, "Inversion must keep the model above its base");
    }
    input.name = "dinnerbone";
    inverted.update(nullptr, nullptr, bones, input);
    require(inverted.matrices()[0][5] == 1 && inverted.matrices()[0][7] == 0, "Renaming must restore the upright pose and names are case sensitive");
    input.name = "Dinnerbone";
    input.identifier = "minecraft:player";
    inverted.update(nullptr, nullptr, bones, input);
    require(inverted.matrices()[0][5] == 1, "Mob inversion must not change player skins");

}
