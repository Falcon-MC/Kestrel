#pragma once

#include "client/Inventory.h"
#include "menu/Hud.h"
#include "ui/JsonUi.h"

#include <functional>
#include <optional>
#include <set>

namespace kestrel::menu {

class InventoryScreen {
public:
    bool active = false;
    bool creativeMode = false;
    InventoryState state;
    std::function<HudSlot(const HudItem&)> itemIcon;
    std::function<void(ui::Context&, const std::string&, const ui::Rect&, float)> entityRenderer;

    void open();
    void requestOpen() { send(InventoryAction::Open); }
    void close();
    void reset();
    bool handleKeys(const InputState& input, Key inventoryKey);
    void draw(ui::Context& ui, float width, float height, const std::function<void(float, float, float)>& player);
    std::vector<InventoryCommand> takeCommands();
    void setDefinitions(std::shared_ptr<const ui::JsonUi> definitions);

private:
    struct JsonDataKey {
        uint64_t revision = 0;
        uint64_t openRevision = 0;
        int tab = 0;
        int page = 0;
        int primary = 0;
        int secondary = 0;
        bool creative = false;
        bool recipes = false;
        bool wide = false;
        bool craftable = false;
        bool signing = false;
        std::string search;
        std::string title;
        std::set<int> groups;
        uint64_t language = 0;
        bool operator==(const JsonDataKey&) const = default;
    };
    std::optional<JsonDataKey> jsonDataKey;
    ui::UiData jsonData;
    uint64_t jsonDataGeneration = 0;
    std::vector<HudItem> jsonItems;
    std::set<int> jsonGhostSlots;
    std::vector<int> jsonCatalogEntries;
    std::vector<int> jsonCatalogGroups;
    bool drawJson(ui::Context& ui, float width, float height, const std::function<void(float, float, float)>& player);
    std::shared_ptr<const ui::JsonUi> definitions;
    std::unique_ptr<ui::JsonUiScreen> jsonScreen;
    std::string jsonRoot;
    std::string jsonTitle;
    uint64_t jsonOpenRevision = 0;
    int bookPage = 0;
    bool bookSigning = false;
    std::string bookTitle;
    int beaconPrimary = 0;
    int beaconSecondary = 0;
    void send(InventoryAction action, int slot = -1, int value = 0, bool all = false);
    bool book = true;
    bool wideCreative = false;
    bool craftableOnly = false;
    bool searchFocused = false;
    bool dragging = false;
    int dragStart = -1;
    std::vector<int> dragSlots;
    int tab = 0;
    int scrollRow = 0;
    int hoveredSlot = -1;
    int lastClickedSlot = -1;
    double lastClick = 0.0;
    std::string search;
    std::set<int> expandedGroups;
    std::vector<InventoryCommand> commands;
};

}
