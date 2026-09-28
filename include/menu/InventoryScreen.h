#pragma once

#include "client/Inventory.h"
#include "menu/Hud.h"

#include <functional>
#include <set>

namespace kestrel::menu {

class InventoryScreen {
public:
    bool active = false;
    bool creativeMode = false;
    InventoryState state;
    std::function<HudSlot(const HudItem&)> itemIcon;

    void open();
    void requestOpen() { send(InventoryAction::Open); }
    void close();
    void reset();
    bool handleKeys(const InputState& input, Key inventoryKey);
    void draw(ui::Context& ui, float width, float height, const std::function<void(float, float, float)>& player);
    std::vector<InventoryCommand> takeCommands();

private:
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
