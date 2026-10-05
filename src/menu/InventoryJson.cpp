#include "menu/InventoryScreen.h"

#include "client/ContainerLayout.h"
#include "ui/Localization.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>

namespace kestrel::menu {

namespace {

void drawBanner(ui::Context& ui, const ui::Rect& rect, float alpha, int base, const std::vector<std::pair<std::string, int>>& layers)
{
    static constexpr uint32_t colors[] = { 0x1D1D21, 0xB02E26, 0x5E7C16, 0x835432, 0x3C44AA, 0x8932B8, 0x169C9C, 0x9D9D97,
        0x474F52, 0xF38BAA, 0x80C71F, 0xFED83D, 0x3AB3DA, 0xC74EBD, 0xF9801D, 0xF9FFFE };
    static const char* patterns[] = { "", "bo", "bri", "mc", "cre", "cr", "cbo", "lud", "rd", "ld", "rud", "flo", "gra", "gru", "hh", "hhb",
        "vh", "vhr", "moj", "mr", "sku", "ss", "bl", "br", "tl", "tr", "sc", "bs", "cs", "dls", "drs", "ls", "ms", "rs", "ts", "bt", "tt", "bts", "tts", "glb", "pig", "flw", "gus" };
    const auto& texture = ui.skin().sprite("textures/entity/banner/banner");
    if (!texture.valid) {
        return;
    }
    float scale = texture.width / 512.0f;
    auto paint = [&](int tile, int dye) {
        uint32_t color = colors[std::clamp(dye, 0, 15)];
        ui::Color tint { uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color), uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) };
        ui.spriteRegion(rect, "textures/entity/banner/banner", { (float(tile % 8) * 64.0f + 1.0f) * scale,
            (float(tile / 8) * 64.0f + 1.0f) * scale, 20.0f * scale, 40.0f * scale }, tint);
    };
    paint(0, base);
    for (const auto& [name, color] : layers) {
        auto found = std::find(std::begin(patterns), std::end(patterns), name);
        if (found != std::end(patterns)) {
            paint(int(found - std::begin(patterns)), color);
        }
    }
}

void drawEnchantingBook(ui::Context& ui, const ui::Rect& rect, float alpha, bool opened)
{
    const std::string texture = "textures/entity/enchanting_table_book";
    const auto& sprite = ui.skin().sprite(texture);
    if (!sprite.valid) {
        return;
    }
    float width = rect.w * (opened ? 0.38f : 0.08f);
    float height = std::min(rect.h * 0.7f, rect.w * 0.65f);
    float centerX = rect.x + rect.w * 0.5f;
    float centerY = rect.y + rect.h * 0.5f;
    ui::Color tint { 255, 255, 255, uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) };
    auto leaf = [&](float left, float right, float fold, float u, float v, float w, float h) {
        std::array<std::array<float, 2>, 4> points { {
            { left, centerY - height * 0.5f + fold }, { right, centerY - height * 0.5f },
            { right, centerY + height * 0.5f }, { left, centerY + height * 0.5f + fold }
        } };
        float sx = sprite.width / 64.0f;
        float sy = sprite.height / 32.0f;
        std::array<std::array<float, 2>, 4> uv { { { u * sx, v * sy }, { (u + w) * sx, v * sy },
            { (u + w) * sx, (v + h) * sy }, { u * sx, (v + h) * sy } } };
        ui.spriteQuad(points, texture, uv, tint);
    };
    leaf(centerX - width, centerX, height * 0.08f, 0, 0, 6, 10);
    leaf(centerX, centerX + width, -height * 0.08f, 16, 0, 6, 10);
    if (opened) {
        leaf(centerX - width * 0.9f, centerX, height * 0.07f, 1, 11, 5, 8);
        leaf(centerX, centerX + width * 0.9f, -height * 0.07f, 13, 11, 5, 8);
    }
}

}

void InventoryScreen::setDefinitions(std::shared_ptr<const ui::JsonUi> value)
{
    definitions = std::move(value);
    jsonScreen.reset();
    jsonDataKey.reset();
    jsonRoot.clear();
}

bool InventoryScreen::drawJson(ui::Context& ui, float width, float height, const std::function<void(float, float, float)>& player)
{
    using namespace inventory;
    ContainerLayout layout = containerLayout(state);
    if (!definitions || layout.screen.empty() || !definitions->has(layout.screen)) {
        return false;
    }
    bool customKey = !state.customName.empty() && state.customName.find(' ') == std::string::npos && state.customName.find('.') != std::string::npos;
    std::string customName = customKey ? ui::tr(state.customName, state.customName) : state.customName;
    std::string title = customName.empty() ? ui::tr(layout.title, layout.title) : customName;
    if (!jsonScreen || jsonRoot != layout.screen || jsonTitle != title) {
        jsonRoot = layout.screen;
        jsonTitle = title;
        jsonOpenRevision = state.openRevision;
        ui::UiRow variables;
        // Menu::inventoryLayer already dims the entire window outside the safe area.
        variables["$screen_background_alpha"] = ui::UiValue::of(0.0);
        variables["$container_title"] = ui::UiValue::of(title);
        variables["$localize_title"] = ui::UiValue::of(false);
        variables["$use_smithing_table_2_ui"] = ui::UiValue::of(true);
        variables["$max_page_length"] = ui::UiValue::of(256.0);
        variables["$survival_index"] = ui::UiValue::of(6.0);
        variables["$survival_layout_index"] = ui::UiValue::of(1.0);
        variables["$recipe_book_layout_index"] = ui::UiValue::of(2.0);
        variables["$creative_layout_index"] = ui::UiValue::of(3.0);
        const char* indexes[] = { "$construction_index", "$equipment_index", "$items_index", "$nature_index", "$search_index" };
        for (int index = 0; index < 5; ++index) {
            variables[indexes[index]] = ui::UiValue::of(double(index + 1));
        }
        jsonScreen = std::make_unique<ui::JsonUiScreen>(definitions, layout.screen, variables);
        jsonDataKey.reset();
        jsonScreen->setKeyboardNavigation(true);
        dragging = false;
        dragSlots.clear();
        bookPage = 0;
        bookSigning = false;
        bookTitle = state.bookTitle;
        beaconPrimary = 0;
        beaconSecondary = 0;
    }
    if (!jsonScreen->valid()) {
        return false;
    }
    bool crafting = state.type == ContainerType::Inventory || state.type == ContainerType::Workbench;
    bool shown = crafting && book;
    bool wide = shown && creativeMode && wideCreative;
    const auto& catalog = creativeMode ? state.creative : state.recipes;
    auto& data = jsonData;
    auto& items = jsonItems;
    auto& ghostSlots = jsonGhostSlots;
    auto& catalogEntries = jsonCatalogEntries;
    auto& catalogGroups = jsonCatalogGroups;
    JsonDataKey key { state.revision, state.openRevision, tab, bookPage, beaconPrimary, beaconSecondary,
        creativeMode, book, wideCreative, craftableOnly, bookSigning, search, bookTitle, expandedGroups, ui::Localization::shared().revision() };
    if (!jsonDataKey || *jsonDataKey != key) {
        data = {};
        catalogEntries.clear();
        catalogGroups.clear();
        ghostSlots.clear();
        data.hideUnboundVisibility = true;
        auto boolean = [&](const std::string& name, bool value) {
            data.globals[name] = ui::UiValue::of(value);
        };
        auto number = [&](const std::string& name, double value) {
            data.globals[name] = ui::UiValue::of(value);
        };
        auto text = [&](const std::string& name, const std::string& value) {
            data.globals[name] = ui::UiValue::of(value);
        };
        boolean("#is_survival_layout", !shown);
        boolean("#is_recipe_book_layout", shown && !wide);
        boolean("#is_creative_layout", wide);
        boolean("#is_creative_mode", creativeMode);
        boolean("#close_button_visible", true);
        boolean("#is_creative_layout_button_visible", creativeMode);
        boolean("#is_creative_and_recipe_book_layout", creativeMode && shown && !wide);
        boolean("#is_creative_and_creative_layout", wide);
        boolean("#is_left_tab_inventory", !shown);
        boolean("#filtering_enabled", craftableOnly);
        boolean("#needs_crafting_table", false);
        boolean("#gamepad_helper_visible", false);
        boolean("#show_persistent_bundle_hover_text", true);
        const char* tabs[] = { "construct", "equipment", "items", "nature", "search" };
        const char* visibleTabs[] = { "construction", "equipment", "items", "nature" };
        for (int index = 0; index < 5; ++index) {
            boolean(std::string("#is_left_tab_") + tabs[index], tab == index);
            if (index < 4) {
                boolean(std::string("#") + visibleTabs[index] + "_tab_visible", true);
            }
        }
        text("#crafting_label_text", title);
        text("#container_title", title);
        text("#text_box_item_name", search);
        text("#tab_label_text", title);
        if (state.type == ContainerType::Anvil) {
            text("#text_box_item_name", state.stationName);
            std::string cost = ui::tr("container.repair.cost", "Enchantment Cost: %1");
            size_t marker = cost.find("%1");
            if (marker != std::string::npos) {
                cost.replace(marker, 2, std::to_string(state.stationCost));
            }
            text("#cost_text", state.stationCost > 0 ? cost : std::string());
            boolean("#cost_text_red", state.stationCost > state.experienceLevel && !creativeMode);
            boolean("#cost_text_green", state.stationCost > 0 && (creativeMode || state.stationCost <= state.experienceLevel));
            boolean("#cross_out_icon", !state.slots[Ui + 1].empty() && state.slots[Output].empty());
        }
        boolean("#is_container_screen", state.screen.empty());
        if (state.type == ContainerType::Cartography) {
            const auto& input = state.slots[Ui + 12];
            const auto& material = state.slots[Ui + 13];
            bool map = !input.empty() && (input.identifier == "minecraft:filled_map" || input.identifier == "minecraft:map");
            bool clone = map && material.identifier == "minecraft:map";
            bool extend = map && material.identifier == "minecraft:paper";
            bool locator = map && material.identifier == "minecraft:compass";
            bool locked = map && material.identifier == "minecraft:glass_pane";
            boolean("#is_none_mode", !map);
            boolean("#is_clone_mode", clone);
            boolean("#is_extend_mode", extend);
            boolean("#is_locator_map_mode", locator);
            boolean("#is_locked_mode", locked);
            boolean("#is_basic_map_mode", map && !clone && !extend && !locator && !locked);
            boolean("#is_rename_mode", false);
            text("#output_description", state.slots[Output].empty() ? std::string() : world::itemDisplayName(state.slots[Output].identifier));
        }
        if (state.type == ContainerType::SmithingTable) {
            boolean("#cross_out_icon", !state.slots[Ui + 51].empty() && state.slots[Output].empty());
        }
        if (state.screen == "npc_interact.npc_screen") {
            boolean("#student_view_visible", true);
            text("#title_text", state.customName);
            text("#dialogtext", state.dialogue);
            number("#action_count", double(state.npcButtons.size()));
            for (const auto& [index, label] : state.npcButtons) {
                ui::UiRow button;
                button["#student_button_text"] = ui::UiValue::of(label);
                button["#student_button_visible"] = ui::UiValue::of(true);
                data.collections["student_buttons_collection"].push_back(std::move(button));
            }
        }
        if (state.screen == "book.book_screen" || state.type == ContainerType::Lectern) {
            bookPage = std::clamp(bookPage, 0, std::max(0, int(state.pages.size()) - 1));
            int spread = bookPage / 2 * 2;
            boolean("#viewing", !bookSigning);
            boolean("#signing", bookSigning);
            boolean("#editable", state.bookEditable);
            boolean("#author_editable", false);
            boolean("#prev_page_button_active", spread > 0);
            boolean("#next_page_button_active", spread + 2 < int(state.pages.size()) || (state.bookEditable && state.pages.size() < 50));
            boolean("#finalize_button_enabled", state.bookEditable && !bookTitle.empty());
            text("#title_text_box_item_name", bookTitle);
            text("#author_text_box_item_name", state.author);
            for (int side = 0; side < 2; ++side) {
                int page = spread + side;
                bool exists = page < int(state.pages.size());
                ui::UiRow values;
                values["#page_visible"] = ui::UiValue::of(exists);
                values["#is_text_page"] = ui::UiValue::of(exists);
                values["#editable"] = ui::UiValue::of(state.bookEditable);
                values["#text_box_item_name"] = ui::UiValue::of(exists ? state.pages[page] : std::string());
                values["#page_number"] = ui::UiValue::of(exists ? std::to_string(page + 1) : std::string());
                values["#edit_button_active"] = ui::UiValue::of(state.bookEditable && bookPage != page);
                values["#edit_controls_active"] = ui::UiValue::of(state.bookEditable && bookPage == page);
                values["#insert_page_active"] = ui::UiValue::of(state.bookEditable && state.pages.size() < 50);
                values["#swap_left_active"] = ui::UiValue::of(state.bookEditable && page > 0);
                values["#swap_right_active"] = ui::UiValue::of(state.bookEditable && page + 1 < int(state.pages.size()));
                data.collections["book_pages"].push_back(std::move(values));
            }
        }
        if (state.type == ContainerType::Enchantment) {
            for (int index = 0; index < 3; ++index) {
                ui::UiRow option;
                bool exists = index < int(state.enchantments.size());
                bool enabled = exists && (creativeMode || (state.experienceLevel >= state.enchantments[index].mCost && state.slots[Ui + 15].count > index));
                option["#selectable_button_visibility"] = ui::UiValue::of(enabled);
                option["#unselectable_button_visibility"] = ui::UiValue::of(exists && !enabled);
                option["#selectable_dust_is_visible"] = ui::UiValue::of(enabled);
                option["#unselectable_dust_is_visible"] = ui::UiValue::of(exists && !enabled);
                option["#cost"] = ui::UiValue::of(exists ? std::to_string(state.enchantments[index].mCost) : std::string());
                option["#runes"] = ui::UiValue::of(exists ? state.enchantments[index].mEnchantName : std::string());
                option["#hover_text"] = ui::UiValue::of(exists ? state.enchantments[index].mEnchantName : std::string());
                data.collections["#enchant_buttons"].push_back(std::move(option));
            }
        }
        if (state.type == ContainerType::Beacon) {
            boolean("#supports_netherite", true);
            const char* names[] = { "speed", "haste", "resist", "jump", "strength", "regen", "extra", "confirm", "cancel" };
            const int powers[] = { 1, 3, 11, 8, 5, 10, beaconPrimary, 0, 0 };
            for (int index = 0; index < 9; ++index) {
                bool selected = index < 5 ? beaconPrimary == powers[index] : index < 7 && beaconSecondary == powers[index];
                const int levels[] = { 1, 1, 2, 2, 3, 4, 4, 0, 0 };
                bool enabled = state.beaconLevel >= levels[index] && (index != 7 || (beaconPrimary != 0 && !state.slots[Ui + 27].empty()));
                ui::UiRow button;
                button["#button_visible"] = ui::UiValue::of(true);
                button["#active"] = ui::UiValue::of(enabled && !selected);
                button["#inactive"] = ui::UiValue::of(!enabled);
                button["#selected"] = ui::UiValue::of(enabled && selected);
                data.collections[names[index]].push_back(std::move(button));
            }
        }
        number("#furnace_arrow_ratio", 1.0 - state.furnaceProgress);
        number("#furnace_flame_ratio", 1.0 - state.furnaceFlame);
        auto property = [&](int key, int fallback = 0) {
            auto found = state.properties.find(key);
            return found == state.properties.end() ? fallback : found->second;
        };
        if (state.type == ContainerType::BrewingStand) {
            number("#brewing_arrow_ratio", property(0) > 0 ? std::clamp(property(0) / 400.0, 0.0, 1.0) : 1.0);
            number("#brewing_fuel_ratio", 1.0 - std::clamp(double(property(1)) / std::max(1, property(2, 20)), 0.0, 1.0));
            number("#brewing_bubbles_ratio", property(0) > 0 ? (property(0) % 20) / 20.0 : 1.0);
        }
        if (state.type == ContainerType::Horse) {
            auto equipment = mountSlots(state.mountIdentifier);
            bool horse = equipment.body == MountSlots::Body::HorseArmor;
            bool carpet = equipment.body == MountSlots::Body::Carpet;
            bool nautilus = equipment.body == MountSlots::Body::NautilusArmor;
            boolean("#has_saddle_slot", equipment.saddle);
            boolean("#has_only_carpet_slot", !equipment.saddle && carpet);
            boolean("#has_horse_armor_and_saddle_slot", equipment.saddle && horse);
            boolean("#has_only_horse_armor_slot", !equipment.saddle && horse);
            boolean("#has_carpet_and_saddle_slot", equipment.saddle && carpet);
            boolean("#has_only_nautilus_armor_slot", !equipment.saddle && nautilus);
            boolean("#has_nautilus_armor_and_saddle_slot", equipment.saddle && nautilus);
            boolean("#is_chested", state.containerSize > 2);
            text("#equip_grid_dimensions", "1," + std::to_string(int(equipment.saddle) + int(equipment.body != MountSlots::Body::None)));
            text("#inv_grid_dimensions", std::to_string(std::max(0, state.containerSize - 2) / 3) + ",3");
            number("#entity_id", double(state.mountRuntimeId));
        }
        if (state.type == ContainerType::Crafter) {
            for (int index = 0; index < 9; ++index) {
                boolean("#button_visible" + std::to_string(index), (state.disabledSlots & (1 << index)) != 0);
            }
            text("#redstone_arrow_texture", "textures/ui/redstone_arrow_unpowered");
            number("#crafter_output_item", -1 - Output);
            text("#output_stack_count", state.slots[Output].count > 1 ? std::to_string(state.slots[Output].count) : std::string());
            text("#crafting_preview_info", state.slots[Output].empty() ? std::string() : world::itemDisplayName(state.slots[Output].identifier));
        }
        items.assign(state.slots.begin(), state.slots.end());
        if (crafting) {
            for (int index = 0; index < 9; ++index) {
                int slot = Craft + index;
                if (items[slot].empty() && !state.recipeGhost[index].empty()) {
                    items[slot] = state.recipeGhost[index];
                    ghostSlots.insert(slot);
                }
            }
            if (items[Output].empty() && !state.recipeGhostOutput.empty()) {
                items[Output] = state.recipeGhostOutput;
                ghostSlots.insert(Output);
            }
        }
        auto row = [&](int slot) {
            const HudItem& value = items[slot];
            ui::UiRow result;
            result["#item_renderer_data"] = ui::UiValue::of(double(slot));
            result["#item_id_aux"] = ui::UiValue::of(double(-1 - slot));
            result["#inventory_stack_count"] = ui::UiValue::of(value.count > 1 ? std::to_string(value.count) : std::string());
            result["#item_stack_count"] = result["#inventory_stack_count"];
            std::string tooltip = value.empty() ? std::string() : value.customName.empty() ? world::itemDisplayName(value.identifier) : value.customName;
            static const char* enchantmentKeys[] = {
                "protect.all", "protect.fire", "protect.fall", "protect.explosion", "protect.projectile", "thorns", "oxygen", "waterWalker", "waterWorker",
                "damage.all", "damage.undead", "damage.arthropods", "knockback", "fire", "lootBonus", "digging", "untouching", "durability", "lootBonusDigger",
                "arrowDamage", "arrowKnockback", "arrowFire", "arrowInfinite", "lootBonusFishing", "fishingSpeed", "frostwalker", "mending", "curse.binding",
                "curse.vanishing", "tridentImpaling", "tridentRiptide", "tridentLoyalty", "tridentChanneling", "crossbowMultishot", "crossbowPiercing",
                "crossbowQuickCharge", "soul_speed", "swift_sneak", "heavy_weapon.windburst", "heavy_weapon.density", "heavy_weapon.breach", "lunge"
            };
            for (const auto& [id, level] : value.enchantments) {
                if (id >= 0 && id < int(std::size(enchantmentKeys)) && level > 0) {
                    std::string key = std::string("enchantment.") + enchantmentKeys[id];
                    tooltip += std::string("\n") + (id == 27 || id == 28 ? "\xC2\xA7" "c" : "\xC2\xA7" "7") + ui::tr(key, key);
                    tooltip += " " + ui::tr("enchantment.level." + std::to_string(level), std::to_string(level));
                }
            }
            for (const std::string& line : value.lore) {
                tooltip += '\n' + line;
            }
            if (state.type == ContainerType::Crafter && slot >= Container && slot < Container + 9 && value.empty()) {
                tooltip = ui::tr("gui.togglable_slot", "Toggle Slot");
            }
            result["#hover_text"] = ui::UiValue::of(tooltip);
            result["#is_selected_slot"] = ui::UiValue::of(false);
            result["#container_item_background"] = ui::UiValue::of(0.0);
            for (const char* binding : { "#bundle_selected_item_visible", "#item_storage_visible", "#item_lock", "#item_lock_in_inventory", "#item_lock_in_slot" }) {
                result[binding] = ui::UiValue::of(false);
            }
            result["#empty_armor_image_visible"] = ui::UiValue::of(value.empty());
            result["#empty_offhand_image_visible"] = ui::UiValue::of(value.empty());
            result["#empty_bottle_image_visible"] = ui::UiValue::of(value.empty());
            result["#empty_fuel_image_visible"] = ui::UiValue::of(value.empty());
            result["#empty_image_visible"] = ui::UiValue::of(value.empty());
            result["#item_valid"] = ui::UiValue::of(true);
            int maximum = value.empty() ? 0 : world::itemMaxDurability(value.identifier);
            double durability = maximum > 0 ? std::clamp(double(maximum - value.damage) / maximum, 0.0, 1.0) : 0.0;
            result["#item_durability_visible"] = ui::UiValue::of(maximum > 0 && value.damage > 0);
            result["#item_durability_total_amount"] = ui::UiValue::of(1000.0);
            result["#item_durability_current_amount"] = ui::UiValue::of(durability * 1000.0);
            return result;
        };
        for (const ContainerCollection& collection : layout.collections) {
            auto& rows = data.collections[collection.name];
            for (int slot : collection.slots) {
                rows.push_back(row(slot));
            }
        }
        std::set<int> listedGroups;
        for (const auto& entry : state.stationOptions) {
            int index = int(items.size());
            items.push_back(entry.item);
            auto values = row(index);
            values["#stone_selector_total_items"] = ui::UiValue::of(double(state.stationOptions.size()));
            values["#stone_cell_background_texture"] = ui::UiValue::of(entry.networkId == state.selectedStationRecipe ? "textures/ui/cell_image_invert" : "textures/ui/cell_image_normal");
            data.collections["stones"].push_back(std::move(values));
        }
        number("#stone_selector_total_items", double(state.stationOptions.size()));
        for (size_t index = 0; index < state.loomPatterns.size(); ++index) {
            ui::UiRow option;
            option["#banner_patterns"] = ui::UiValue::of(state.loomPatterns[index]);
            option["#pattern_selector_total_items"] = ui::UiValue::of(double(state.loomPatterns.size()));
            option["#pattern_cell_background_texture"] = ui::UiValue::of(int(index) == state.selectedStationRecipe ? "textures/ui/cell_image_invert" : "textures/ui/cell_image_normal");
            data.collections["patterns"].push_back(std::move(option));
        }
        number("#pattern_selector_total_items", double(state.loomPatterns.size()));
        if (state.type == ContainerType::Trade) {
            int lastTier = state.tradeTier;
            for (const TradeOfferView& offer : state.trades) {
                lastTier = std::max(lastTier, offer.tier);
            }
            std::string level = ui::tr("trade.level." + std::to_string(state.tradeTier + 1), std::to_string(state.tradeTier + 1));
            std::string name = customName.empty() ? ui::tr("entity.villager.name", "Villager") : customName;
            std::string label = ui::tr("trade.nameAndLevel", "%s - %s");
            for (const std::string& part : { name, level }) {
                if (size_t at = label.find("%s"); at != std::string::npos) {
                    label.replace(at, 2, part);
                }
            }
            text("#name_label", label);
            boolean("#show_level", true);
            bool experience = !state.tradeTierExperience.empty();
            boolean("#exp_bar_visible", experience);
            number("#exp_progress", 0.0);
            number("#exp_possible_progress", 0.0);
            boolean("#trade_details_button_1_visible", false);
            boolean("#trade_details_button_2_visible", false);
            boolean("#enchantment_details_button_visible", false);
            boolean("#gamepad_helper_x_visible", false);
            boolean("#gamepad_helper_y_visible", false);
            boolean("#single_slash_visible", false);
            boolean("#double_slash_visible", false);
            boolean("#trade_button_enabled", !state.slots[inventory::Output].empty());
            number("#trade_tier_total", double(lastTier + 1));
            std::vector<int> perTier(size_t(lastTier + 1), 0);
            for (size_t index = 0; index < state.trades.size(); ++index) {
                const TradeOfferView& offer = state.trades[index];
                int tier = std::clamp(offer.tier, 0, lastTier);
                int local = perTier[size_t(tier)]++;
                bool unlocked = tier <= state.tradeTier;
                std::string key = std::to_string(local) + ":" + std::to_string(tier);
                if (int(index) == state.selectedTrade) {
                    data.globals["#radio:trade_toggle"] = ui::UiValue::of(key);
                }
                ui::UiRow trade;
                trade["#trade_toggle_enabled"] = ui::UiValue::of(unlocked);
                trade["#trade_cross_out_visible"] = ui::UiValue::of(offer.soldOut);
                trade["#trade_possible"] = ui::UiValue::of(unlocked && !offer.soldOut && offer.affordable);
                trade["#padding_around_sell_item"] = ui::UiValue::of(offer.buyB.empty());
                trade["#hover_text"] = ui::UiValue::of(std::string());
                data.collections["trades:" + std::to_string(tier)].push_back(std::move(trade));
                auto cell = [&](const std::string& collection, const HudItem& item, int count, int original) {
                    int itemIndex = int(items.size());
                    items.push_back(item);
                    auto values = row(itemIndex);
                    values["#trade_item_count"] = ui::UiValue::of(item.empty() || count <= 1 ? std::string() : std::to_string(count));
                    values["#trade_price_different"] = ui::UiValue::of(!item.empty() && original != count);
                    values["#second_trade_item_count"] = ui::UiValue::of(std::to_string(original));
                    data.collections[collection + ":" + key].push_back(std::move(values));
                };
                cell("trade_item_1", offer.buyA, offer.countA, offer.originalCountA);
                cell("trade_item_2", offer.buyB, offer.countB, offer.originalCountB);
                cell("sell_item", offer.sell, offer.sell.count, offer.sell.count);
            }
            for (int tier = 0; tier <= lastTier; ++tier) {
                ui::UiRow values;
                values["#tier_name"] = ui::UiValue::of(ui::tr("trade.level." + std::to_string(tier + 1), std::to_string(tier + 1)));
                values["#is_tier_unlocked"] = ui::UiValue::of(tier <= state.tradeTier);
                values["#tier_visible"] = ui::UiValue::of(perTier[size_t(tier)] > 0);
                values["#trade_tier_total"] = ui::UiValue::of(double(perTier[size_t(tier)]));
                values["#trade_cell_background_texture"] = ui::UiValue::of(std::string("textures/ui/cell_image_normal"));
                data.collections["trade_tiers"].push_back(std::move(values));
            }
        }
        if (shown && catalog) {
            std::string query = search;
            std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            for (size_t index = 0; index < catalog->size(); ++index) {
                const InventoryCatalogItem& entry = (*catalog)[index];
                bool available = creativeMode || std::find(state.craftable.begin(), state.craftable.end(), entry.networkId) != state.craftable.end();
                if (craftableOnly && !available) {
                    continue;
                }
                const int categories[] = { 1, 3, 4, 2, 0 };
                if (tab != 4 && entry.category != categories[tab]) {
                    continue;
                }
                std::string label = world::itemDisplayName(entry.item.identifier);
                std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (!query.empty() && label.find(query) == std::string::npos && entry.item.identifier.find(query) == std::string::npos) {
                    continue;
                }
                bool grouped = creativeMode && query.empty() && tab != 4 && entry.group >= 0 && !entry.groupName.empty();
                if (grouped) {
                    if (listedGroups.insert(entry.group).second) {
                        int icon = int(items.size());
                        items.push_back(entry.item);
                        auto header = row(icon);
                        for (const char* binding : { "#inventory_stack_count", "#item_stack_count", "#recipe_craftable_count" }) {
                            header[binding] = ui::UiValue::of(std::string());
                        }
                        header["#container_item_background"] = ui::UiValue::of(expandedGroups.contains(entry.group) ? 2.0 : 1.0);
                        header["#recipe_hover_text"] = ui::UiValue::of(ui::tr(entry.groupName, entry.groupName));
                        header["#container_item_background_texture"] = ui::UiValue::of(expandedGroups.contains(entry.group)
                            ? "textures/ui/recipe_book_dark_button_pressed" : "textures/ui/recipe_book_light_button");
                        data.collections["recipe_book"].push_back(std::move(header));
                        catalogEntries.push_back(int(index));
                        catalogGroups.push_back(entry.group);
                    }
                    if (!expandedGroups.contains(entry.group)) {
                        continue;
                    }
                }
                int item = static_cast<int>(items.size());
                items.push_back(entry.item);
                ui::UiRow values = row(item);
                values["#container_item_background"] = ui::UiValue::of(!available ? 5.0 : grouped ? 3.0 : 0.0);
                values["#craftable"] = ui::UiValue::of(available);
                values["#recipe_item"] = ui::UiValue::of(true);
                values["#is_group"] = ui::UiValue::of(false);
                for (const char* binding : { "#inventory_stack_count", "#item_stack_count", "#recipe_craftable_count" }) {
                    values[binding] = ui::UiValue::of(std::string());
                }
                values["#recipe_hover_text"] = values["#hover_text"];
                values["#is_creative_selected_slot"] = ui::UiValue::of(false);
                values["#container_item_background_texture"] = ui::UiValue::of(!available ? "textures/ui/recipe_book_red_button"
                    : grouped ? "textures/ui/recipe_book_dark_button" : "textures/ui/recipe_book_item_bg");
                data.collections["recipe_book"].push_back(std::move(values));
                catalogEntries.push_back(static_cast<int>(index));
                catalogGroups.push_back(-1);
            }
        }
        number("#recipe_book_length", double(catalogEntries.size()));
        number("#recipe_book_total_items", double(catalogEntries.size()));
        key.page = bookPage;
        jsonDataKey = std::move(key);
        ++jsonDataGeneration;
    }
    auto drawItem = [&](const HudItem& value, const ui::Rect& rect, float alpha) {
        if (value.empty() || !itemIcon) {
            return;
        }
        HudSlot icon = itemIcon(value);
        ui::Color tint { 255, 255, 255, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) };
        if (!icon.icon.empty()) {
            ui.sprite(rect, icon.icon, tint);
        }
    };
    jsonScreen->setRenderer([&](ui::Context&, const std::string& renderer, const ui::Rect& rect, float alpha, const ui::UiLookup& lookup) {
        if (renderer == "inventory_item_renderer") {
            ui::UiValue key = lookup("#item_renderer_data");
            int index = -1;
            if (key.kind == ui::UiValue::Kind::None) {
                key = lookup("#item_id_aux");
                int packed = static_cast<int>(key.toNumber());
                if (packed < 0) {
                    index = -1 - packed;
                } else {
                    static const std::pair<int, const char*> payments[] = {
                        { 48627712, "minecraft:netherite_ingot" }, { 25427968, "minecraft:emerald" },
                        { 17301504, "minecraft:diamond" }, { 17432576, "minecraft:gold_ingot" }, { 17367040, "minecraft:iron_ingot" }
                    };
                    for (const auto& [id, identifier] : payments) {
                        if (packed == id) {
                            HudItem icon;
                            icon.identifier = identifier;
                            icon.count = 1;
                            drawItem(icon, rect, alpha);
                            return;
                        }
                    }
                }
            } else {
                index = static_cast<int>(key.toNumber());
            }
            if (key.kind != ui::UiValue::Kind::None && index >= 0 && index < static_cast<int>(items.size())) {
                drawItem(items[index], rect, ghostSlots.contains(index) ? alpha * 0.625f : alpha);
            }
        } else if (renderer == "live_player_renderer") {
            float pixel = std::min(rect.w, rect.h) / 16.0f;
            if (pixel > 0.0f) {
                player(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f - (32.0f - 1.62f * 16.0f) * pixel, pixel);
            }
        } else if (renderer == "paper_doll_renderer" || renderer == "inventory_player_renderer") {
            player(rect.x + rect.w * 0.5f, rect.y, rect.h / 32.0f);
        } else if (renderer == "live_horse_renderer" && entityRenderer) {
            entityRenderer(ui, state.mountIdentifier, rect, alpha);
        } else if (renderer == "banner_pattern_renderer") {
            std::string pattern = lookup("#banner_patterns").toText();
            const HudItem& banner = state.slots[Output].empty() ? state.slots[Ui + 9] : state.slots[Output];
            if (!pattern.empty()) {
                drawBanner(ui, rect, alpha, 15, { { pattern, 0 } });
            } else if (!banner.empty()) {
                drawBanner(ui, rect, alpha, banner.aux, banner.bannerPatterns);
            }
        } else if (renderer == "enchanting_book_renderer") {
            drawEnchantingBook(ui, rect, alpha, !state.slots[Ui + 14].empty());
        }
    });
    jsonScreen->draw(ui, { 0.0f, 0.0f, width, height }, data, jsonDataGeneration);
    jsonScreen->setRenderer({});
    ui::UiEvent target = jsonScreen->pointerTarget();
    hoveredSlot = -1;
    for (const ContainerCollection& collection : layout.collections) {
        if (collection.name == target.collection && target.index >= 0 && target.index < static_cast<int>(collection.slots.size())) {
            hoveredSlot = collection.slots[target.index];
            break;
        }
    }
    if (hoveredSlot < 0 && state.type == ContainerType::Trade && target.collection.empty()
        && (target.name.starts_with("button.trade_take") || target.name == "button.trade_coalesce_stack")) {
        hoveredSlot = inventory::Output;
    }
    const InputState& input = ui.input();
    if (active && !ui.isBlocked() && !jsonScreen->editing()) {
        if (hoveredSlot >= 0) {
            if (input.pressedKey >= Key::Num1 && input.pressedKey <= Key::Num9) {
                send(InventoryAction::HotbarSwap, hoveredSlot, int(input.pressedKey) - int(Key::Num1));
            }
            if (input.pressedKey == Key::Q) {
                send(InventoryAction::Drop, hoveredSlot, 0, input.isHeld(Key::Control));
            }
        }
        if (input.mousePressed || input.rightMousePressed) {
            bool secondary = input.rightMousePressed;
            double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (hoveredSlot >= 0 && !target.name.starts_with("disabled_slot_")) {
                if (creativeMode && target.name == "button.container_auto_destroy") {
                    send(InventoryAction::Destroy, hoveredSlot);
                } else if (state.type == ContainerType::Crafter && hoveredSlot >= Container && hoveredSlot < Container + 9
                    && state.slots[hoveredSlot].empty() && state.slots[inventory::Cursor].empty()) {
                    send(InventoryAction::ToggleCrafter, hoveredSlot - Container);
                } else if (input.isHeld(Key::Shift)) {
                    send(InventoryAction::QuickMove, hoveredSlot);
                } else if (!secondary && lastClickedSlot == hoveredSlot && now - lastClick < 0.3 && !state.slots[inventory::Cursor].empty()) {
                    send(InventoryAction::Collect, hoveredSlot);
                } else if (secondary || state.slots[inventory::Cursor].empty() || hoveredSlot == Output) {
                    send(secondary ? InventoryAction::Secondary : InventoryAction::Primary, hoveredSlot);
                } else {
                    dragging = true;
                    dragStart = hoveredSlot;
                    dragSlots = { hoveredSlot };
                }
                lastClickedSlot = hoveredSlot;
                lastClick = now;
            } else if (target.collection == "recipe_book" && catalog && target.index >= 0 && target.index < static_cast<int>(catalogEntries.size())) {
                int group = catalogGroups[target.index];
                if (group >= 0) {
                    if (expandedGroups.contains(group)) {
                        expandedGroups.erase(group);
                    } else {
                        expandedGroups.insert(group);
                    }
                } else {
                    const auto& entry = (*catalog)[catalogEntries[target.index]];
                    send(creativeMode ? InventoryAction::Creative : InventoryAction::SelectRecipe,
                        creativeMode && input.isHeld(Key::Shift) ? AnyInventorySlot : -1, entry.networkId, !secondary);
                }
            } else if (state.screen.empty() && target.name.empty() && !jsonScreen->pointerInsideContent(ui.mouseX(), ui.mouseY())) {
                send(InventoryAction::Drop, inventory::Cursor, 0, !secondary);
            }
        }
        if (dragging) {
            if (hoveredSlot >= 0 && hoveredSlot != Output && std::find(dragSlots.begin(), dragSlots.end(), hoveredSlot) == dragSlots.end()) {
                dragSlots.push_back(hoveredSlot);
            }
            if (!input.mouseDown) {
                if (dragSlots.size() == 1) {
                    send(InventoryAction::Primary, dragStart);
                } else {
                    commands.push_back({ InventoryAction::Distribute, -1, 0, true, dragSlots });
                }
                dragging = false;
                dragSlots.clear();
            }
        }
    }
    for (const ui::UiEvent& event : jsonScreen->takeEvents()) {
        if (!active || ui.isBlocked()) {
            continue;
        }
        if (event.kind == ui::UiEvent::Kind::Button && input.enter) {
            bool handled = false;
            for (const auto& collection : layout.collections) {
                if (collection.name == event.collection && event.index >= 0 && event.index < int(collection.slots.size())) {
                    InventoryAction action = creativeMode && event.name == "button.container_auto_destroy" ? InventoryAction::Destroy
                        : input.isHeld(Key::Shift) ? InventoryAction::QuickMove : InventoryAction::Primary;
                    send(action, collection.slots[event.index]);
                    handled = true;
                    break;
                }
            }
            if (handled) {
                continue;
            }
        }
        if (creativeMode && event.name == "button.destroy_selection") {
            send(InventoryAction::Destroy, inventory::Cursor);
        } else if (state.type == ContainerType::Crafter && event.name.starts_with("disabled_slot_")) {
            for (int index = 0; index < 9; ++index) {
                if (event.name == "disabled_slot_" + std::to_string(index) + "_button") {
                    send(InventoryAction::ToggleCrafter, index);
                }
            }
        } else if (event.name == "button.pattern_select" && event.index >= 0 && event.index < int(state.loomPatterns.size())) {
            send(InventoryAction::StationRecipe, -1, event.index);
        } else if (state.type == ContainerType::Trade && event.kind == ui::UiEvent::Kind::Toggle && event.name.find("trade_toggle") != std::string::npos && event.index >= 0) {
            int tier = std::max(event.outerIndex, 0);
            int local = 0;
            for (size_t index = 0; index < state.trades.size(); ++index) {
                if (std::max(state.trades[index].tier, 0) != tier) {
                    continue;
                }
                if (local++ == event.index) {
                    send(InventoryAction::StationRecipe, -1, int(index));
                    break;
                }
            }
        } else if (state.type == ContainerType::Trade && event.name == "button.trade") {
            send(InventoryAction::Craft, inventory::Output, 0);
        } else if (event.name == "button.stone_select" && event.index >= 0 && event.index < int(state.stationOptions.size())) {
            send(InventoryAction::StationRecipe, -1, state.stationOptions[event.index].networkId);
        } else if (event.name == "button.student_button" && event.index >= 0) {
            send(InventoryAction::NpcAction, -1, event.index);
        } else if (state.type == ContainerType::Enchantment && event.collection == "#enchant_buttons" && event.kind == ui::UiEvent::Kind::Button) {
            send(InventoryAction::Enchant, -1, event.index);
        } else if (state.type == ContainerType::Beacon && event.kind == ui::UiEvent::Kind::Button) {
            const char* names[] = { "speed", "haste", "resist", "jump", "strength", "regen" };
            const int powers[] = { 1, 3, 11, 8, 5, 10 };
            for (int index = 0; index < 6; ++index) {
                if (event.collection == names[index]) {
                    (index == 5 ? beaconSecondary : beaconPrimary) = powers[index];
                }
            }
            if (event.collection == "extra") {
                beaconSecondary = beaconPrimary;
            } else if (event.collection == "confirm") {
                send(InventoryAction::Beacon, beaconPrimary, beaconSecondary);
            } else if (event.collection == "cancel") {
                close();
            }
        } else if (state.screen == "book.book_screen" && event.kind == ui::UiEvent::Kind::Text) {
            if (bookSigning) {
                bookTitle = event.text;
            } else {
                int page = bookPage / 2 * 2 + std::clamp(event.index, 0, 1);
                commands.push_back({ InventoryAction::BookPage, page, -3, false, {}, event.text });
            }
        } else if (event.name == "button.prev_page") {
            bookPage = std::max(0, bookPage / 2 * 2 - 2);
            if (state.type == ContainerType::Lectern) {
                send(InventoryAction::BookPage, bookPage);
            }
        } else if (event.name == "button.next_page") {
            int next = bookPage / 2 * 2 + 2;
            if (next >= int(state.pages.size()) && state.bookEditable && state.pages.size() < 50) {
                for (int page = int(state.pages.size()); page <= next && page < 50; ++page) {
                    commands.push_back({ InventoryAction::BookPage, page, -1 });
                }
            }
            bookPage = next;
            if (state.type == ContainerType::Lectern) {
                send(InventoryAction::BookPage, bookPage);
            }
        } else if (event.name == "button.sign_book") {
            bookSigning = true;
        } else if (event.name == "button.edit_page") {
            bookPage = bookPage / 2 * 2 + std::clamp(event.index, 0, 1);
        } else if (event.name == "button.finalize") {
            commands.push_back({ InventoryAction::BookSign, -1, 0, false, {}, bookTitle });
            close();
        } else if (event.name == "button.insert_text_page" || event.name == "button.delete_page" || event.name == "button.swap_page_left" || event.name == "button.swap_page_right") {
            int page = bookPage / 2 * 2 + std::clamp(event.index, 0, 1);
            int operation = event.name == "button.insert_text_page" ? -1 : event.name == "button.delete_page" ? -2 : event.name == "button.swap_page_left" ? page - 1 : page + 1;
            commands.push_back({ InventoryAction::BookPage, page, operation });
        } else if (event.kind == ui::UiEvent::Kind::Text && state.type == ContainerType::Anvil) {
            commands.push_back({ InventoryAction::Rename, -1, 0, false, {}, event.text });
        } else if (event.kind == ui::UiEvent::Kind::Text && crafting) {
            search = event.text;
            tab = 4;
        } else if (event.kind == ui::UiEvent::Kind::Toggle && event.name == "layout_toggle" && event.state) {
            int selected = static_cast<int>(event.value);
            book = selected != 1;
            wideCreative = selected == 3;
        } else if (event.kind == ui::UiEvent::Kind::Toggle && event.name == "navigation_tab" && event.state) {
            tab = std::clamp(static_cast<int>(event.value) - 1, 0, 4);
        } else if (event.kind == ui::UiEvent::Kind::Toggle && (event.name == "craftable_toggle" || event.name == "toggle.enableFiltering")) {
            craftableOnly = event.state;
        } else if (event.name == "button.menu_exit" || event.name == "button.try_menu_exit" || event.name == "button.menu_inventory_exit" || event.name == "button.exit_student" || event.name == "button.book_exit") {
            close();
        }
    }
    if (!state.slots[inventory::Cursor].empty()) {
        ui.clearClip();
        drawItem(state.slots[inventory::Cursor], { ui.mouseX() - 8.0f, ui.mouseY() - 8.0f, 16.0f, 16.0f }, 1.0f);
        if (state.slots[inventory::Cursor].count > 1) {
            ui.pixelTextScaled(std::to_string(state.slots[inventory::Cursor].count), ui.mouseX(), ui.mouseY() + 2.0f, 1.0f, { 255, 255, 255, 255 });
        }
    }
    return true;
}

}
