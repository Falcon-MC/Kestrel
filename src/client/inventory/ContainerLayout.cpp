#include "client/ContainerLayout.h"

#include <algorithm>
#include <numeric>

namespace kestrel {

MountSlots mountSlots(const std::string& identifier)
{
    if (identifier == "minecraft:skeleton_horse") {
        return { false, MountSlots::Body::None };
    }
    if (identifier.find("llama") != std::string::npos) {
        return { false, MountSlots::Body::Carpet };
    }
    if (identifier.find("nautilus") != std::string::npos) {
        return { true, MountSlots::Body::NautilusArmor };
    }
    if (identifier == "minecraft:donkey" || identifier == "minecraft:mule" || identifier.find("camel") != std::string::npos) {
        return { true, MountSlots::Body::None };
    }
    return {};
}

int personalUiSlot(int slot, ContainerType type)
{
    if (slot == 0) {
        return inventory::Cursor;
    }
    if (slot == 50) {
        return inventory::Output;
    }
    int start = type == ContainerType::Workbench ? 32 : 28;
    int count = type == ContainerType::Workbench ? 9 : 4;
    if (slot >= start && slot < start + count) {
        return inventory::Craft + slot - start;
    }
    return slot >= 0 && slot < 54 ? inventory::Ui + slot : -1;
}

int personalUiIndex(int slot, ContainerType type)
{
    if (slot == inventory::Cursor) {
        return 0;
    }
    if (slot == inventory::Output) {
        return 50;
    }
    if (slot >= inventory::Craft && slot < inventory::Cursor) {
        return slot - inventory::Craft + (type == ContainerType::Workbench ? 32 : 28);
    }
    return slot >= inventory::Ui && slot < inventory::SlotCount ? slot - inventory::Ui : -1;
}

ContainerSlotType personalSlotType(int index, ContainerType type)
{
    switch (index) {
    case 1: return ContainerSlotType::AnvilInput;
    case 2: return ContainerSlotType::AnvilMaterial;
    case 3: return ContainerSlotType::StonecutterInput;
    case 9: return ContainerSlotType::LoomInput;
    case 10: return ContainerSlotType::LoomDye;
    case 11: return ContainerSlotType::LoomMaterial;
    case 12: return ContainerSlotType::CartographyInput;
    case 13: return ContainerSlotType::CartographyAdditional;
    case 14: return ContainerSlotType::EnchantingInput;
    case 15: return ContainerSlotType::EnchantingMaterial;
    case 16: return ContainerSlotType::GrindstoneInput;
    case 17: return ContainerSlotType::GrindstoneAdditional;
    case 27: return ContainerSlotType::BeaconPayment;
    case 51: return ContainerSlotType::SmithingTableInput;
    case 52: return ContainerSlotType::SmithingTableMaterial;
    case 53: return ContainerSlotType::SmithingTableTemplate;
    default: return ContainerSlotType::Unknown;
    }
}

ContainerLayout containerLayout(const InventoryState& state)
{
    using namespace inventory;
    ContainerLayout result;
    auto run = [&](const std::string& name, int start, int count) {
        ContainerCollection collection { name, std::vector<int>(std::max(0, count)) };
        std::iota(collection.slots.begin(), collection.slots.end(), start);
        result.collections.push_back(std::move(collection));
    };
    auto cell = [&](const std::string& name, int raw) {
        result.collections.push_back({ name, { personalUiSlot(raw, state.type) } });
    };
    run("inventory_items", 9, 27);
    run("hotbar_items", 0, 9);
    if (!state.screen.empty()) {
        result.screen = state.screen;
        result.title = state.customName;
        result.collections.clear();
        return result;
    }
    switch (state.type) {
    case ContainerType::Inventory:
    case ContainerType::Workbench:
        result.screen = state.type == ContainerType::Workbench ? "crafting.crafting_screen" : "crafting.inventory_screen";
        result.title = "container.crafting";
        run("crafting_input_items", Craft, state.type == ContainerType::Workbench ? 9 : 4);
        run("crafting_output_items", Output, 1);
        run("armor_items", Armor, 4);
        run("offhand_items", Offhand, 1);
        break;
    case ContainerType::Container:
    case ContainerType::MinecartChest:
    case ContainerType::ChestBoat:
        result.screen = state.containerSize > 27 ? "chest.large_chest_screen" : "chest.small_chest_screen";
        result.title = state.containerSize > 27 ? "container.chestDouble" : "container.chest";
        if (state.enderChest) {
            result.screen = "chest.ender_chest_screen";
            result.title = "container.enderchest";
        } else if (state.blockIdentifier.ends_with("barrel")) {
            result.screen = "chest.barrel_screen";
            result.title = "container.barrel";
        } else if (state.blockIdentifier.find("shulker_box") != std::string::npos) {
            result.screen = "chest.shulker_box_screen";
            result.title = "container.shulkerbox";
        }
        run("container_items", Container, std::clamp(state.containerSize, 0, 54));
        break;
    case ContainerType::Furnace:
    case ContainerType::BlastFurnace:
    case ContainerType::Smoker: {
        std::string name = state.type == ContainerType::Furnace ? "furnace" : state.type == ContainerType::Smoker ? "smoker" : "blast_furnace";
        result.screen = name + "." + name + "_screen";
        result.title = "container." + name;
        run("furnace_ingredient_items", Container, 1);
        run("furnace_fuel_items", Container + 1, 1);
        run("furnace_output_items", Container + 2, 1);
        break;
    }
    case ContainerType::BrewingStand:
        result.screen = "brewing_stand.brewing_stand_screen";
        result.title = "container.brewing";
        run("brewing_input_item", Container, 1);
        run("brewing_result_items", Container + 1, 3);
        run("brewing_fuel_item", Container + 4, 1);
        break;
    case ContainerType::Anvil:
        result.screen = "anvil.anvil_screen";
        result.title = "container.repair";
        cell("anvil_input_items", 1);
        cell("anvil_material_items", 2);
        cell("anvil_result_items", 50);
        break;
    case ContainerType::Enchantment:
        result.screen = "enchanting.enchanting_screen";
        result.title = "container.enchant";
        cell("enchanting_input_items", 14);
        cell("enchanting_lapis_items", 15);
        break;
    case ContainerType::Grindstone:
        result.screen = "grindstone.grindstone_screen";
        result.title = "container.grindstone_title";
        cell("grindstone_input_items", 16);
        cell("grindstone_additional_items", 17);
        cell("grindstone_result_items", 50);
        break;
    case ContainerType::Loom:
        result.screen = "loom.loom_screen";
        result.title = "container.loom";
        cell("loom_input_items", 9);
        cell("loom_dye_items", 10);
        cell("loom_material_items", 11);
        cell("loom_result_items", 50);
        break;
    case ContainerType::SmithingTable:
        result.screen = "smithing_table.smithing_table_screen";
        result.title = "container.smithing_table";
        cell("smithing_table_template_items", 53);
        cell("smithing_table_input_items", 51);
        cell("smithing_table_material_items", 52);
        cell("smithing_table_result_items", 50);
        break;
    case ContainerType::Cartography:
        result.screen = "cartography.cartography_screen";
        result.title = "container.cartography_table";
        cell("cartography_input_items", 12);
        cell("cartography_additional_items", 13);
        cell("cartography_result_items", 50);
        break;
    case ContainerType::Stonecutter:
        result.screen = "stonecutter.stonecutter_screen";
        result.title = "container.stonecutter";
        cell("stonecutter_input_items", 3);
        cell("stonecutter_result_items", 50);
        break;
    case ContainerType::Beacon:
        result.screen = "beacon.beacon_screen";
        result.title = "container.beacon";
        cell("beacon_payment_items", 27);
        break;
    case ContainerType::Hopper:
    case ContainerType::MinecartHopper:
    case ContainerType::Dispenser:
    case ContainerType::Dropper:
    case ContainerType::Crafter: {
        std::string name = state.type == ContainerType::Dispenser ? "dispenser" : state.type == ContainerType::Dropper ? "dropper" : state.type == ContainerType::Crafter ? "crafter" : "hopper";
        result.screen = "redstone." + name + "_screen";
        result.title = "container." + name;
        run("container_items", Container, name == "hopper" ? 5 : 9);
        break;
    }
    case ContainerType::Horse: {
        result.screen = "horse.horse_screen";
        auto slots = mountSlots(state.mountIdentifier);
        std::string identifier = state.mountIdentifier.empty() ? "horse" : state.mountIdentifier.substr(state.mountIdentifier.find(':') + 1);
        result.title = "entity." + identifier + ".name";
        int count = int(slots.saddle) + int(slots.body != MountSlots::Body::None);
        run("horse_equip_items", slots.saddle ? Container : Container + 1, count);
        run("container_items", Container + 2, std::clamp(state.containerSize - 2, 0, 15));
        break;
    }
    case ContainerType::Lectern:
        result.screen = "book.book_screen";
        result.title = "book.editTitle";
        break;
    default:
        break;
    }
    return result;
}

}
