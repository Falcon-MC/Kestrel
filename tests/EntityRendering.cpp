#include "client/ActorEquipment.h"
#include "render/Renderer.h"
#include "world/EntityAnimation.h"
#include "world/Geometry.h"
#include "Core/Json/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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
    static_assert(sizeof(ActorDraw::constants) == 240);
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
    input.armorItems[1] = "minecraft:leather_horse_armor";
    input.armorColors[1] = 0x123456;
    animator.update(nullptr, nullptr, bones, input);
    require(expression(animator, "query.armor_texture_slot(1)") == 1, "Horse armor must select its authored leather texture");
    require(std::abs(expression(animator, "query.armor_color_slot(1, 0)") - 18.0 / 255) < 1e-6, "Authored armor colors must read the equipped stack dye");

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
