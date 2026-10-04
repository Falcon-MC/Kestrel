#include "menu/Menu.h"

#include "platform/Input.h"
#include "ui/Context.h"
#include "ui/JsonUi.h"
#include "ui/Localization.h"

#include <algorithm>

namespace kestrel::menu {

using namespace ui;

namespace {

constexpr const char* EmoteRoot = "persona_emote.emote_wheel_screen";
constexpr const char* EmoteEquipRoot = "persona_popups.popup_dialog__emote_equip_slot_editor";
// The live player in a slot is framed like the pause screen's paper doll.
constexpr float PreviewModelPixels = 32.0f;
constexpr float PreviewFramePixels = 39.4f;

}

void Menu::setEmotes(std::vector<EmoteOption> options)
{
    emoteOptions = std::move(options);
}

void Menu::setEmoteSlots(const std::array<std::string, EmoteSlotCount>& slots)
{
    emoteSlotIds = slots;
}

/**
 * The emote each slot shows: the one the player put there, or else the next
 * emote no other slot holds, so a fresh wheel fills itself.
 */
std::array<const EmoteOption*, EmoteSlotCount> Menu::filledEmoteSlots() const
{
    std::array<const EmoteOption*, EmoteSlotCount> filled {};
    auto find = [&](const std::string& id) -> const EmoteOption* {
        auto found = std::find_if(emoteOptions.begin(), emoteOptions.end(), [&](const EmoteOption& option) { return option.id == id; });
        return found == emoteOptions.end() ? nullptr : &*found;
    };
    for (size_t slot = 0; slot < EmoteSlotCount; ++slot) {
        filled[slot] = emoteSlotIds[slot].empty() ? nullptr : find(emoteSlotIds[slot]);
    }
    size_t next = 0;
    for (size_t slot = 0; slot < EmoteSlotCount; ++slot) {
        while (!filled[slot] && next < emoteOptions.size()) {
            const EmoteOption* candidate = &emoteOptions[next++];
            if (std::find(filled.begin(), filled.end(), candidate) == filled.end()) {
                filled[slot] = candidate;
            }
        }
    }
    return filled;
}


/**
 * A slot picked on the wheel plays its emote and closes the wheel; on the
 * equip popup it takes the emote being equipped, swapping places with the
 * slot already holding it, and the wheel comes back.
 */
void Menu::pickEmoteSlot(size_t slot)
{
    std::array<const EmoteOption*, EmoteSlotCount> filled = filledEmoteSlots();
    if (emoteEquipping.empty()) {
        if (filled[slot]) {
            emoteRequest = filled[slot]->id;
            dialog = Dialog::None;
        }
        return;
    }
    for (size_t other = 0; other < EmoteSlotCount; ++other) {
        emoteSlotIds[other] = filled[other] ? filled[other]->id : std::string();
    }
    auto previous = std::find(emoteSlotIds.begin(), emoteSlotIds.end(), emoteEquipping);
    if (previous != emoteSlotIds.end()) {
        std::swap(*previous, emoteSlotIds[slot]);
    } else {
        emoteSlotIds[slot] = emoteEquipping;
    }
    emoteSlotsChanged = true;
    emoteEquipping.clear();
    if (emoteEquipOnly) {
        dialog = Dialog::None;
    }
}

/**
 * The emote "Change Emotes" offers to equip: the first one the wheel does not
 * show, or else the one after the emote offered last.
 */
void Menu::beginEmoteEquip()
{
    if (emoteOptions.empty()) {
        return;
    }
    std::array<const EmoteOption*, EmoteSlotCount> filled = filledEmoteSlots();
    for (const EmoteOption& option : emoteOptions) {
        if (std::find(filled.begin(), filled.end(), &option) == filled.end()) {
            emoteEquipping = option.id;
            return;
        }
    }
    size_t next = 0;
    for (size_t i = 0; i < emoteOptions.size(); ++i) {
        if (emoteOptions[i].id == lastEmoteOffered) {
            next = (i + 1) % emoteOptions.size();
        }
    }
    emoteEquipping = emoteOptions[next].id;
    lastEmoteOffered = emoteEquipping;
}

/**
 * emote_wheel_screen.json, or the equip popup while an emote is being placed,
 * fed the way the game's emote screen controller feeds them: strict data,
 * the hovered emote's name, each slot showing the live player or the emote's
 * icon, and the instructions for the device in use.
 */
void Menu::emoteWheel(Context& ui, float width, float height)
{
    if (!jsonUi) {
        dialog = Dialog::None;
        return;
    }
    std::string root = emoteEquipping.empty() ? EmoteRoot : EmoteEquipRoot;
    bool opening = !emoteUi;
    if (!emoteUi || root != emoteRoot) {
        emoteUi = std::make_unique<JsonUiScreen>(jsonUi, root);
        emoteRoot = root;
        emoteHovered = -1;
    }
    if (opening && !emoteEquipOnly) {
        emoteEquipping.clear();
    }
    if (!emoteUi->valid()) {
        emoteUi.reset();
        dialog = Dialog::None;
        return;
    }
    std::array<const EmoteOption*, EmoteSlotCount> filled = filledEmoteSlots();
    UiData data;
    data.hideUnboundVisibility = true;
    UiRow& globals = data.globals;
    globals["#is_using_mouse"] = UiValue::of(true);
    globals["#is_using_keyboard"] = UiValue::of(true);
    globals["#is_touch_mode"] = UiValue::of(false);
    globals["#is_using_gamepad"] = UiValue::of(false);
    globals["#is_using_gamepad_override"] = UiValue::of(false);
    globals["#dressing_room_button_visible"] = UiValue::of(emoteEquipping.empty());
    const EmoteOption* hovered = emoteHovered >= 0 ? filled[size_t(emoteHovered)] : nullptr;
    globals["#emote_name"] = UiValue::of(hovered ? hovered->name : std::string());
    std::string equipping;
    for (const EmoteOption& option : emoteOptions) {
        if (option.id == emoteEquipping) {
            equipping = option.name;
        }
    }
    globals["#emote_popup_title"] = UiValue::of(equipping);
    globals["#emote_screen_instructions"] = UiValue::of(tr("emotes.instructions_keyboard", ""));
    globals["#emote_screen_exit"] = UiValue::of(tr("controller.buttonTip.back", "Back"));
    for (size_t slot = 0; slot < EmoteSlotCount; ++slot) {
        std::string suffix = ":" + std::to_string(slot);
        const EmoteOption* option = filled[slot];
        globals["#emote_is_valid" + suffix] = UiValue::of(option != nullptr);
        globals["#image_is_valid" + suffix] = UiValue::of(option != nullptr);
        globals["#emote_image" + suffix] = UiValue::of(option ? option->icon : std::string());
        globals["#emote_image_file_system" + suffix] = UiValue::of(std::string("InAppPackage"));
        globals["#emote_index_name" + suffix] = UiValue::of(option ? option->name : std::string());
        globals["#emote_name_touch" + suffix] = UiValue::of(option ? option->name : std::string());
    }

    emoteUi->draw(ui, { 0.0f, 0.0f, width, height }, data);
    // A slot without an icon of its own shows the player, the way the game previews an emote on the character.
    std::vector<Rect> previews = emoteUi->controlRects("emote_preview");
    for (size_t slot = 0; slot < previews.size() && slot < EmoteSlotCount; ++slot) {
        const Rect& rect = previews[slot];
        if (!filled[slot] || !filled[slot]->icon.empty() || rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        float pixel = std::min(rect.h / PreviewFramePixels, rect.w / (PreviewModelPixels * 0.5f));
        modelPose = &filled[slot]->pose;
        playerModel(ui, rect.x + rect.w * 0.5f, rect.y + rect.h - pixel * PreviewModelPixels, pixel);
        modelPose = nullptr;
    }

    for (const UiEvent& event : emoteUi->takeEvents()) {
        if (event.kind != UiEvent::Kind::Button) {
            continue;
        }
        if (event.name == "button.emote_hovered" || event.name == "button.emote_hovered_via_analog") {
            emoteHovered = event.index;
        } else if (event.name == "button.emote_selected" && event.index >= 0 && event.index < int(EmoteSlotCount)) {
            pickEmoteSlot(size_t(event.index));
        } else if (event.name == "button.dressing_room") {
            beginEmoteEquip();
        } else if (event.name == "button.close_dialog" || event.name == "button.close_emote_popup") {
            emoteEquipping.clear();
            if (emoteEquipOnly) {
                dialog = Dialog::None;
            }
        } else if (event.name == "button.menu_exit" || event.name == "button.emote_wheel_exit_non_gamepad") {
            dialog = Dialog::None;
        }
    }
    const InputState& input = ui.input();
    int digit = int(input.pressedKey) - int(Key::Num1);
    if (dialog == Dialog::Emotes && digit >= 0 && digit < int(EmoteSlotCount)) {
        pickEmoteSlot(size_t(digit));
    }
    if (dialog == Dialog::Emotes && !opening && input.pressedKey == bindings.emote()) {
        dialog = Dialog::None;
    }
    if (dialog != Dialog::Emotes) {
        emoteUi.reset();
        emoteEquipping.clear();
        emoteEquipOnly = false;
    }
}

}

namespace kestrel::menu {

/**
 * Opens the equip popup for one emote straight away, the way equipping an
 * emote from the dressing room asks which slot it goes in; placing it or
 * closing the popup goes back to where the player was.
 */
void Menu::openEmoteEquip(const std::string& id)
{
    emoteUi.reset();
    emoteEquipping = id;
    emoteEquipOnly = true;
    dialog = Dialog::Emotes;
}

}
