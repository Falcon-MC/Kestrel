#include "menu/Menu.h"
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/JsonUi.h"
#include "ui/Theme.h"

#include <algorithm>

namespace kestrel::menu {
using namespace ui;
using namespace ui::theme;

void Menu::globalResourcesPage(Context& ui, float x, float& y, float w)
{
    using Kind = world::GlobalPackAction::Kind;
    auto action = [&](Kind kind, std::string id = {}, std::string value = {}) {
        if (!globalPacksBusy) globalPackActions.push_back({ kind, std::move(id), std::move(value) });
    };
    settingsHeading(ui, x, y, w, tr("menu.globalpacks", "Global Resources"),
        tr("kestrel.globalResources.description", "Apply resource packs to menus and every server. Higher packs take priority."));
    float left = x + 12, width = w - 24, gap = 6, button = (width - gap * 2) / 3;
    if (ui.pressableButton("global:import", "pressableElevatedPrimary", tr("kestrel.globalResources.import", "Import"), { left, y, button, 20 }, TextStyle::Ui, !globalPacksBusy)) action(Kind::Import);
    if (ui.pressableButton("global:reload", "pressableElevatedSecondary", tr("kestrel.globalResources.reload", "Reload"), { left + button + gap, y, button, 20 }, TextStyle::Ui, !globalPacksBusy)) action(Kind::Reload);
    if (ui.pressableButton("global:folder", "pressableElevatedSecondary", tr("kestrel.globalResources.folder", "Folder"), { left + (button + gap) * 2, y, button, 20 }, TextStyle::Ui, !globalPacksBusy)) action(Kind::OpenFolder);
    y += 27;
    if (globalPacksBusy) y += ui.paragraph(tr("kestrel.globalResources.loading", "Loading resource packs..."), TextStyle::Ui, left, y, width, Muted0) + 6;
    else if (!globalPacksStatus.empty()) y += ui.paragraph(globalPacksStatus, TextStyle::Ui, left, y, width, Muted0) + 6;
    size_t active = std::count_if(globalPacks.begin(), globalPacks.end(), [](const auto& entry) { return entry.active; });
    float half = (width - gap) / 2;
    if (ui.pressableButton("global:active", showActiveGlobalPacks ? "pressableElevatedPrimary" : "pressableElevatedSecondary",
        tr("resourcePack.selected.title", "Active") + " (" + std::to_string(active) + ")", { left, y, half, 24 })) {
        showActiveGlobalPacks = true; openedGlobalPack.clear(); removingGlobalPack.clear();
    }
    if (ui.pressableButton("global:available", !showActiveGlobalPacks ? "pressableElevatedPrimary" : "pressableElevatedSecondary",
        tr("resourcePack.available.title", "My Packs") + " (" + std::to_string(globalPacks.size() - active) + ")", { left + half + gap, y, half, 24 })) {
        showActiveGlobalPacks = false; openedGlobalPack.clear(); removingGlobalPack.clear();
    }
    y += 31;
    size_t position = 0, displayed = 0;
    for (const auto& entry : globalPacks) {
        if (entry.active != showActiveGlobalPacks) continue;
        ++displayed;
        bool opened = entry.id == openedGlobalPack;
        const std::string root = entry.active ? "resource_packs.selected_pack_button" : "resource_packs.available_pack_button";
        bool clicked = false;
        if (jsonUi && jsonUi->has(root)) {
            auto& screen = globalPackScreens[entry.id + root];
            if (!screen) {
                UiRow variables;
                variables["$available_pack_items"] = UiValue::of(std::string("global_packs"));
                variables["$selected_pack_items"] = UiValue::of(std::string("global_packs"));
                variables["$button.available_pack"] = UiValue::of(std::string("button.global_pack"));
                variables["$button.selected_pack"] = UiValue::of(std::string("button.global_pack"));
                variables["$button.read_toggle"] = UiValue::of(std::string("button.global_pack"));
                screen = std::make_unique<JsonUiScreen>(jsonUi, root, variables);
            }
            UiData data;
            auto& row = data.collections["global_packs"].emplace_back();
            row["#name"] = UiValue::of(entry.name);
            // Expanded details are drawn below the card, once, with measured height.
            row["#description"] = UiValue::of(std::string());
            row["#size"] = UiValue::of(std::to_string(entry.bytes / 1024) + " KB");
            row["#icon_path"] = UiValue::of(entry.icon ? "dynamic/global_pack/" + entry.id : std::string("textures/ui/missing_pack_icon"));
            row["#icon_zip"] = UiValue::of(std::string());
            row["#icon_file_system"] = UiValue::of(std::string("RawPath"));
            row["#is_read_more"] = UiValue::of(true);
            row["#is_read_less"] = UiValue::of(false);
            screen->draw(ui, { left, y, width, 44 }, data);
            for (const auto& event : screen->takeEvents()) if (event.kind == UiEvent::Kind::Button) clicked = true;
            if (opened) ui.outline({ left, y, width, 44 }, White);
        } else {
            clicked = ui.pressableButton("global:pack:" + entry.id, opened ? "pressableElevatedPrimary" : "pressableElevatedSecondary", "", { left, y, width, 44 });
            ui.sprite({ left + 1, y, 42, 42 }, entry.icon ? "dynamic/global_pack/" + entry.id : "textures/ui/missing_pack_icon");
            Color ink = opened ? White : InkDark;
            ui.text(entry.name, TextStyle::Ui, left + 48, y + 1, ink, width - 54);
            ui.text(entry.version, TextStyle::Ui, left + 48, y + 19, ink, width - 54);
        }
        if (clicked) {
            openedGlobalPack = opened ? std::string() : entry.id;
            removingGlobalPack.clear();
        }
        y += 48;
        if (opened) {
            if (!entry.description.empty()) y += ui.paragraph(entry.description, TextStyle::Ui, left + 4, y, width - 8, White) + 7;
            if (!entry.error.empty()) y += ui.paragraph(entry.error, TextStyle::Ui, left + 4, y, width - 8, Muted0) + 7;
            if (entry.error.empty()) {
                if (ui.pressableButton("global:toggle:" + entry.id, "pressableElevatedPrimary",
                    entry.active ? tr("kestrel.globalResources.deactivate", "Deactivate") : tr("kestrel.globalResources.activate", "Activate"), { left, y, width - (entry.active ? 52 : 0), 20 }, TextStyle::Ui, !globalPacksBusy))
                    action(entry.active ? Kind::Deactivate : Kind::Activate, entry.id);
                if (entry.active) {
                    if (position > 0 && ui.pressableButton("global:up:" + entry.id, "pressableElevatedSecondary", "", { left + width - 46, y, 20, 20 })) action(Kind::Up, entry.id);
                    if (position > 0) ui.sprite({ left + width - 43, y + 3, 14, 14 }, "textures/ui/up_arrow");
                    if (position + 1 < active && ui.pressableButton("global:down:" + entry.id, "pressableElevatedSecondary", "", { left + width - 20, y, 20, 20 })) action(Kind::Down, entry.id);
                    if (position + 1 < active) ui.sprite({ left + width - 17, y + 3, 14, 14 }, "textures/ui/down_arrow");
                }
                y += 26;
                if (!entry.subPacks.empty()) {
                    std::string current = tr("kestrel.globalResources.default", "Default");
                    size_t next = 0;
                    for (size_t i = 0; i < entry.subPacks.size(); ++i) if (entry.subPacks[i].first == entry.selectedSubPack) { current = entry.subPacks[i].second; next = i + 1; }
                    if (ui.pressableButton("global:variant:" + entry.id, "pressableElevatedSecondary", current, { left, y, width, 20 }))
                        action(Kind::SubPack, entry.id, next < entry.subPacks.size() ? entry.subPacks[next].first : std::string());
                    y += 26;
                }
            }
            bool removing = removingGlobalPack == entry.id;
            if (removing) {
                y += ui.paragraph(trf("kestrel.globalResources.confirmDescription", "Delete %s permanently?", { entry.name }), TextStyle::Ui, left, y, width, White) + 6;
            }
            if (ui.pressableButton("global:remove:" + entry.id, removing ? "pressableElevatedDestructive" : "pressableElevatedSecondary",
                removing ? tr("kestrel.globalResources.confirm", "Confirm") : tr("kestrel.globalResources.delete", "Delete"), { left, y, removing ? half : width, 20 }, TextStyle::Ui, !globalPacksBusy)) {
                if (removing) { action(Kind::Remove, entry.id); removingGlobalPack.clear(); openedGlobalPack.clear(); }
                else removingGlobalPack = entry.id;
            }
            if (removing && ui.pressableButton("global:cancel:" + entry.id, "pressableElevatedSecondary", tr("gui.cancel", "Cancel"), { left + half + gap, y, half, 20 })) removingGlobalPack.clear();
            y += 28;
        }
        ++position;
    }
    if (showActiveGlobalPacks) {
        ui.sprite({ left + 1, y, 42, 42 }, "textures/ui/glyph_resource_pack");
        ui.text("Minecraft", TextStyle::Ui, left + 48, y + 3, White, width - 54);
        ui.text(tr("kestrel.globalResources.base", "Default resources"), TextStyle::Ui, left + 48, y + 19, Muted0, width - 54);
        y += 48;
    } else if (!displayed) y += ui.paragraph(tr("kestrel.globalResources.empty", "Import a .mcpack or .zip file to add a resource pack."), TextStyle::Ui, left, y, width, Muted0) + 12;
}
}
