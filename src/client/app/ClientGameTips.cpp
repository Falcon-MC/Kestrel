#include "client/Client.h"

#include "client/DebugLog.h"
#include "platform/Paths.h"
#include "platform/Window.h"
#include "ui/Localization.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string_view>

namespace kestrel {

namespace {

constexpr double TutorialTipTimeout = 10.0;
constexpr double DismountTipTimeout = 5.0;
constexpr double TipLinger = 1.0;
constexpr double TipGap = 1.0;
constexpr float CameraTipTravel = 40.0f;
constexpr int32_t CreativeGameType = 1;
constexpr int32_t SpectatorGameType = 6;
constexpr uint8_t WaterMedium = 1;
constexpr const char* ShownTipsFile = "shown_tips.txt";
constexpr const char* CraftingTable = "minecraft:crafting_table";

/**
 * A tip ready to show: its id, the lang key of its text, its animation and
 * what a %s in the text stands for.
 */
struct TipRequest {
    std::string id;
    std::string key;
    std::string animation;
    std::string argument;
};

/**
 * The mounts left with the jump key; every other one is left by sneaking.
 */
bool jumpDismounts(std::string_view entity)
{
    return entity == "boat" || entity == "chest_boat" || entity == "minecart" || entity == "raft";
}

void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

bool dismountTip(const std::string& id)
{
    return id.rfind("dismount-", 0) == 0;
}

/**
 * The text of a tip in the client language, with each :_input_key.name:
 * made the key bound to it and %s made argument. Empty when the language has
 * no such key.
 */
std::string tipText(const std::string& key, const KeyBindings& bindings, const std::string& argument)
{
    if (!ui::Localization::shared().has(key)) {
        return {};
    }
    std::string text = ui::tr(key, key);
    replaceAll(text, "~LINEBREAK~", "\n");
    const std::pair<const char*, Key> placeholders[] = {
        { ":_input_key.forward:", bindings.forward() },
        { ":_input_key.back:", bindings.back() },
        { ":_input_key.left:", bindings.left() },
        { ":_input_key.right:", bindings.right() },
        { ":_input_key.jump:", bindings.up() },
        { ":_input_key.sneak:", bindings.down() },
        { ":_input_key.inventory:", bindings.inventory() },
        { ":_input_key.chat:", bindings.chat() },
    };
    for (const auto& [placeholder, bound] : placeholders) {
        replaceAll(text, placeholder, keyName(bound));
    }
    replaceAll(text, "%1$s", argument);
    replaceAll(text, "%s", argument);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) {
        text.pop_back();
    }
    return text;
}

}

/**
 * Reads the ids of the tips already shown on this install, one per line.
 */
void Client::loadShownTips()
{
    shownTipsLoaded = true;
    std::ifstream file(platform::dataDirectory() / ShownTipsFile);
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            shownTips.insert(line);
        }
    }
}

/**
 * Remembers a tip as shown for good, appending its id to the file.
 */
void Client::markTipShown(const std::string& id)
{
    if (!shownTips.insert(id).second) {
        return;
    }
    std::ofstream file(platform::dataDirectory() / ShownTipsFile, std::ios::app);
    if (!file) {
        debugLog("could not record shown tip " + id);
        return;
    }
    file << id << '\n';
}

void Client::showGameTip(const std::string& id, const std::string& text, const std::string& animation, double now)
{
    gameTip = {};
    gameTip.id = id;
    gameTip.view = { text, animation, ++gameTipSerial };
    gameTip.shownAt = now;
    tipLookTravel = 0.0f;
    markTipShown(id);
}

/**
 * Shows the game tips the way the game teaches a new player, one at a time
 * and each once for the whole install: the dismount hint when first riding,
 * the flying and swimming tips when they first apply, the crafting table tips
 * and the ledge sneak tip, then movement, camera, jump, hotbar, breaking,
 * placing, the inventory and chat in turn. A tip goes away a moment after
 * the player does what it teaches, when it times out, or for a dismount hint
 * when the player gets off.
 */
void Client::updateGameTips()
{
    if (!shownTipsLoaded) {
        loadShownTips();
    }
    double now = secondsNow();
    bool inWorld = worldShown && terrainReleased && playerView.active && hudState.gameType != SpectatorGameType;
    if (!inWorld) {
        if (!gameTip.id.empty()) {
            gameTip = {};
            gameTipEndedAt = now;
        }
        return;
    }
    const InputState& input = window->input();
    const KeyBindings& bindings = menu.keyBindings();
    bool playing = menu.capturesMouse() && !mods->wantsCursor();
    bool creative = hudState.gameType == CreativeGameType;
    bool inWater = timeState.cameraMedium == WaterMedium;
    const std::string& held = hudState.inventory[static_cast<size_t>(std::clamp(hudState.selectedSlot, 0, 8))].identifier;
    auto pressing = [&](Key key) {
        return playing && input.isHeld(key);
    };

    if (!gameTip.id.empty()) {
        const std::string& id = gameTip.id;
        if (playing) {
            tipLookTravel += std::abs(input.mouseDeltaX) + std::abs(input.mouseDeltaY);
        }
        bool done = false;
        if (id == "movement") {
            done = pressing(bindings.forward()) || pressing(bindings.back()) || pressing(bindings.left()) || pressing(bindings.right());
        } else if (id == "camera") {
            done = tipLookTravel >= CameraTipTravel;
        } else if (id == "jump" || id == "swim" || id == "fly-up") {
            done = pressing(bindings.up());
        } else if (id == "sneak" || id == "fly-down") {
            done = pressing(bindings.down());
        } else if (id == "hotbar") {
            done = hudState.selectedChanged > gameTip.shownAt;
        } else if (id == "break-block") {
            done = playing && input.mouseDown && selectionView.has_value();
        } else if (id == "place-block" || id == "crafting-table-place") {
            done = playing && input.rightMousePressed && selectionView.has_value();
        } else if (id == "crafting-table-use") {
            done = (playing && input.rightMousePressed && targetBlockName == CraftingTable) || menu.inventoryOpen();
        } else if (id == "open-inventory" || id == "open-inventory-creative") {
            done = menu.inventoryOpen();
        } else if (id == "chat-open") {
            done = menu.currentDialog() == menu::Dialog::Chat;
        } else if (id == "fly") {
            done = playerView.flying;
        } else if (id == "stop-fly") {
            done = !playerView.flying;
        } else if (dismountTip(id)) {
            done = ridingView.empty();
        }
        if (done && gameTip.doneAt == 0.0) {
            gameTip.doneAt = now;
        }
        double timeout = dismountTip(id) ? DismountTipTimeout : TutorialTipTimeout;
        bool gone = now - gameTip.shownAt >= timeout || (gameTip.doneAt > 0.0 && now - gameTip.doneAt >= TipLinger) || (dismountTip(id) && ridingView.empty());
        if (gone) {
            gameTip = {};
            gameTipEndedAt = now;
        }
        return;
    }
    if (now - gameTipEndedAt < TipGap) {
        return;
    }

    auto unseen = [&](const char* id) {
        return !shownTips.contains(id);
    };
    std::optional<TipRequest> next;
    if (!ridingView.empty()) {
        std::string entity = ridingView.substr(ridingView.find(':') == std::string::npos ? 0 : ridingView.find(':') + 1);
        bool jump = jumpDismounts(entity);
        std::string id = jump ? "dismount-jump" : "dismount-sneak";
        if (unseen(id.c_str())) {
            next = TipRequest { id, "action.hint.exit.console." + entity, jump ? "jump-mouse" : "sneak-mouse", {} };
        }
    } else if (creative && playerView.mayFly && (unseen("fly") || unseen("fly-up") || unseen("fly-down") || unseen("stop-fly"))) {
        if (!playerView.flying && unseen("fly")) {
            next = TipRequest { "fly", "gameTip.flying.mouse", "fly-mouse", {} };
        } else if (playerView.flying && unseen("fly-up")) {
            next = TipRequest { "fly-up", "gameTip.flyUp.mouse", "fly-up-mouse", {} };
        } else if (playerView.flying && unseen("fly-down")) {
            next = TipRequest { "fly-down", "gameTip.flyDown.mouse", "fly-down-mouse", {} };
        } else if (playerView.flying && !unseen("fly-up") && !unseen("fly-down") && unseen("stop-fly")) {
            next = TipRequest { "stop-fly", "gameTip.stopFlying.mouse", "stop-fly-mouse", {} };
        }
    }
    if (!next && ridingView.empty() && inWater && unseen("swim")) {
        next = TipRequest { "swim", "gameTip.swim.mouse", "swim-mouse", {} };
    }
    if (!next && ridingView.empty() && targetBlockName == CraftingTable && unseen("crafting-table-use")) {
        next = TipRequest { "crafting-table-use", "gameTip.useCraftingTable.mouse", "crafting-table-use-mouse", {} };
    }
    if (!next && ridingView.empty() && held == CraftingTable && unseen("crafting-table-place")) {
        next = TipRequest { "crafting-table-place", "gameTip.placeCraftingTable.mouse", "crafting-table-place-mouse", {} };
    }
    if (!next && ridingView.empty() && unseen("sneak") && nearbyBlocks && playerView.onGround && !playerView.sneaking && !playerView.flying && !inWater) {
        std::array<float, 3> look = camera.forward();
        float length = std::sqrt(look[0] * look[0] + look[2] * look[2]);
        if (length > 0.01f) {
            double aheadX = playerView.current[0] + look[0] / length * 0.8;
            double aheadZ = playerView.current[2] + look[2] / length * 0.8;
            int32_t cellX = static_cast<int32_t>(std::floor(aheadX));
            int32_t cellZ = static_cast<int32_t>(std::floor(aheadZ));
            int32_t ground = static_cast<int32_t>(std::floor(playerView.current[1])) - 1;
            bool drop = true;
            for (int32_t depth = 0; depth < 3 && drop; ++depth) {
                drop = nearbyBlocks->name(cellX, ground - depth, cellZ) == "minecraft:air";
            }
            if (drop) {
                next = TipRequest { "sneak", "gameTip.useSneak.mouse", "sneak-mouse", {} };
            }
        }
    }
    if (!next && ridingView.empty()) {
        bool zqsd = bindings.forward() == Key::Z && bindings.left() == Key::Q && bindings.back() == Key::S && bindings.right() == Key::D;
        bool inventoryShown = !unseen("open-inventory") || !unseen("open-inventory-creative");
        std::string chatKey = keyName(bindings.chat());
        if (unseen("movement")) {
            next = TipRequest { "movement", "gameTip.playerMovement.mouse", zqsd ? "movement-ZQSD-mouse" : "movement-mouse", {} };
        } else if (unseen("camera")) {
            next = TipRequest { "camera", "gameTip.cameraMovement.mouse", "camera-mouse", {} };
        } else if (unseen("jump")) {
            next = TipRequest { "jump", "gameTip.jump.mouse", "jump-mouse", {} };
        } else if (unseen("hotbar")) {
            next = TipRequest { "hotbar", "gameTip.hotbar.selection.mouse", "hotbar-mouse", "1-9" };
        } else if (unseen("break-block")) {
            if (selectionView) {
                next = TipRequest { "break-block", "gameTip.breakBlock.mouse", "break-block-mouse", {} };
            }
        } else if (unseen("place-block")) {
            if (selectionView && !held.empty()) {
                next = TipRequest { "place-block", "gameTip.placeBlock.mouse", "place-block-mouse", {} };
            }
        } else if (!inventoryShown) {
            next = creative
                ? TipRequest { "open-inventory-creative", "gameTip.openInventoryCreative.mouse", "open-inventory-creative-mouse", {} }
                : TipRequest { "open-inventory", "gameTip.openInventorySurvival.mouse", "open-inventory-mouse", {} };
        } else if (unseen("chat-open")) {
            next = TipRequest { "chat-open", "gameTip.openChat.mouse", "chat-open-mouse", chatKey };
        }
    }
    if (!next) {
        return;
    }
#if defined(KESTREL_IOS)
    if (size_t at = next->key.find(".mouse"); at != std::string::npos) next->key.replace(at, 6, ".touch");
    if (size_t at = next->animation.find("-mouse"); at != std::string::npos) next->animation.replace(at, 6, "-touch");
#endif
    std::string text = tipText(next->key, bindings, next->argument);
    if (text.empty()) {
        return;
    }
    showGameTip(next->id, text, next->animation, now);
}

}
