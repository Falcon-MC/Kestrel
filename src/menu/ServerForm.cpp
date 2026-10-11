#include "menu/Menu.h"
#include "menu/PlayLayout.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;
using namespace play;

namespace {

// The game's modal header and the gaps modalFrame leaves around its content.
constexpr float ModalChrome = 22.0f + 2.0f + 16.0f;
// 80rem, the max-width of the online play warning route.
constexpr float ModalWidth = 300.0f;
// 16rem, the min-height of the online play warning body.
constexpr float WarningBodyHeight = 60.0f;
// 12 base2Scale, the size of the delete and save button icons.
constexpr float ButtonIconSize = css(24.0f);

void centeredParagraph(Context& ui, std::string_view text, TextStyle style, const Rect& area, Color color)
{
    std::vector<std::string_view> lines;
    ui.wrap(text, style, area.w, lines);
    float lineHeight = ui.lineHeight(style);
    float y = area.y;
    for (std::string_view line : lines) {
        ui.textCentered(line, style, { area.x, y, area.w, lineHeight }, color);
        y += lineHeight;
    }
}

/**
 * The icon the game puts before the label of the delete and save buttons,
 * drawn left of the label the button centers.
 */
void buttonIcon(Context& ui, const Rect& button, std::string_view label, std::string_view sprite)
{
    float labelWidth = ui.measure(label, TextStyle::Ui);
    float x = std::round(button.x + (button.w - labelWidth) * 0.5f - ButtonIconSize - 2.0f);
    float y = std::round(button.y + (button.h - ButtonIconSize) * 0.5f);
    ui.sprite({ x, y, ButtonIconSize, ButtonIconSize }, sprite);
}

}

void Menu::serverForm(Context& ui, float width, float height)
{
    (void)height;
    if (field != Field::ServerPort) {
        editPort = clampPort(editPort);
    }
    header(ui, width, editing ? tr("hbui.PlayScreen.serverTab.serverForm.editServerTitle", "Edit server") : tr("hbui.PlayScreen.serverTab.serverForm.newServerTitle", "Add a new server"), false);
    float panelWidth = std::min(522.67f, width - 16.0f);
    constexpr float RowStep = 50.0f;
    Rect panel { std::round((width - panelWidth) * 0.5f), 53.33f, panelWidth, 191.33f };
    ui.fill(panel, Panel);

    struct Entry {
        Field field;
        const char* id;
        std::string label;
        std::string placeholder;
        const std::string* value;
    };
    Entry entries[] = {
        { Field::ServerName, "name", tr("hbui.PlayScreen.serverTab.serverForm.serverNameLabel", "Server name"), tr("hbui.PlayScreen.serverTab.serverForm.serverNamePlaceholder", "Example server name"), &editName },
        { Field::ServerAddress, "address", tr("hbui.PlayScreen.serverTab.serverForm.serverAddressLabel", "Server address"), tr("hbui.PlayScreen.serverTab.serverForm.serverAddressPlaceholder", "IP (1.0.0.1) or URL (www.example.com)"), &editAddress },
        { Field::ServerPort, "port", tr("hbui.PlayScreen.serverTab.serverForm.serverPortLabel", "Port"), "19132", &editPort },
    };
    float y = panel.y;
    for (const Entry& entry : entries) {
        ui.text(entry.label, TextStyle::Ui, panel.x + 12.0f, y + 6.0f, White);
        Rect input { panel.x + 12.0f, y + 18.67f, panel.w - 24.0f, 22.67f };
        if (textField(ui, std::string("form:") + entry.id, entry.placeholder, *entry.value, input, field == entry.field)) {
            field = entry.field;
        }
        y += RowStep;
        ui.fill({ panel.x, y - 3.0f, panel.w, 3.0f }, PanelDark);
    }

    Rect left { panel.x + 4.33f, panel.y + 159.33f, panel.w * 0.5f - 7.0f, ButtonHeight };
    Rect right { panel.x + panel.w * 0.5f + 2.67f, left.y, left.w, ButtonHeight };
    if (editing) {
        std::string remove = tr("hbui.PlayScreen.serverTab.serverForm.deleteServerButton", "Delete server");
        std::string save = tr("hbui.PlayScreen.serverTab.serverForm.saveChangesButton", "Save changes");
        if (ui.pressableButton("form:delete", "pressableElevatedDestructive", remove, left)) {
            selection = Selection { ServerGroup::Saved, *editing };
            dialog = Dialog::ConfirmDelete;
        }
        buttonIcon(ui, left, remove, "hbui/delete");
        if (ui.pressableButton("form:save", "pressableElevatedPrimary", save, right)) {
            saveServerForm(false);
        }
        buttonIcon(ui, right, save, "hbui/save");
    } else {
        if (ui.pressableButton("form:add", "pressableElevatedSecondary", tr("hbui.PlayScreen.serverTab.serverForm.addServerButton", "Add server"), left)) {
            saveServerForm(false);
        }
        if (ui.pressableButton("form:play", "pressableElevatedPrimary", tr("hbui.PlayScreen.serverTab.serverForm.addAndPlayButton", "Add and play"), right)) {
            saveServerForm(true);
        }
    }
}

bool Menu::serverFormChanged() const
{
    if (editing && *editing < store.servers().size()) {
        const SavedServer& server = store.servers()[*editing];
        auto [host, port] = splitAddress(server.address, "19132");
        return editName != server.name || editAddress != host || editPort != port;
    }
    return !editName.empty() || !editAddress.empty() || editPort != "19132";
}

/**
 * The game's server modals: a heading with a close button, centered text and
 * a row of a secondary button and a primary one, or a lone secondary button
 * when primary is empty.
 */
void Menu::serverFormModal(Context& ui, float width, float height, std::string_view heading, std::string_view body, std::string_view secondary, std::string_view primary, std::string_view primaryComponent, bool& secondaryPressed, bool& primaryPressed, bool& closed)
{
    float textWidth = std::min(ModalWidth, width - 16.0f) - 18.0f;
    float bodyHeight = ui.paragraphHeight(body, TextStyle::Ui, textWidth);
    Rect content = modalFrame(ui, width, height, ModalWidth, ModalChrome + bodyHeight + 8.0f + ButtonHeight, heading, closed);
    centeredParagraph(ui, body, TextStyle::Ui, content, White);
    Rect buttons { content.x, content.bottom() - ButtonHeight, content.w, ButtonHeight };
    if (primary.empty()) {
        secondaryPressed = ui.pressableButton("servermodal:secondary", "pressableElevatedSecondary", secondary, buttons);
        primaryPressed = false;
        return;
    }
    Rect left { buttons.x, buttons.y, std::floor(buttons.w * 0.5f) - 2.0f, ButtonHeight };
    Rect right { left.right() + 4.0f, buttons.y, buttons.w - left.w - 4.0f, ButtonHeight };
    secondaryPressed = ui.pressableButton("servermodal:secondary", "pressableElevatedSecondary", secondary, left);
    primaryPressed = ui.pressableButton("servermodal:primary", primaryComponent, primary, right);
}

void Menu::serverFormErrorDialog(Context& ui, float width, float height, bool& closed)
{
    std::string message;
    switch (serverFormProblem) {
    case ServerFormProblem::NameIsEmpty:
        message = tr("hbui.PlayScreen.serverTab.addServerError.nameError", "Name can't be empty");
        break;
    case ServerFormProblem::AddressIsEmpty:
        message = tr("hbui.PlayScreen.serverTab.addServerError.addressError", "Address can't be empty");
        break;
    case ServerFormProblem::InvalidPortNumber:
        message = tr("hbui.PlayScreen.serverTab.addServerError.portError", "The port number entered is not correct");
        break;
    case ServerFormProblem::DuplicateAddressAndPort:
        message = tr("hbui.PlayScreen.serverTab.addServerError.duplicateAddressAndPortError", "A server with this address and port has already been added.");
        break;
    }
    bool close = false;
    bool unused = false;
    serverFormModal(ui, width, height, tr("hbui.PlayScreen.serverTab.addServerError.title", "Something went wrong"), message,
        tr("hbui.PlayScreen.serverTab.addServerError.closeButton", "Close"), {}, {}, close, unused, closed);
    closed = closed || close;
}

/**
 * The game's /online-play-warning route. Proceeding sets the option that
 * stops the warning for that kind of server, the only way the game offers
 * to stop it, then joins.
 */
void Menu::onlinePlayWarningDialog(Context& ui, float width, float height, bool& closed)
{
    if (!warnedRow) {
        closed = true;
        return;
    }
    bool external = warnedRow->group == ServerGroup::Saved;
    std::string heading = external ? tr("hbui.PlayScreen.onlinePlayWarning.externalIP.header", "Caution: Third-party online play")
                                   : tr("hbui.PlayScreen.onlinePlayWarning.multiplayerGeneric.header", "Online play is not rated");
    std::string body = external ? tr("hbui.PlayScreen.onlinePlayWarning.externalIP.body", "Caution: Online play is offered by third-party servers that are not owned, operated, or supervised by Mojang Studios or Microsoft. During online play, you may be exposed to unmoderated chat messages or other types of user-generated content that may not be suitable for everyone.")
                                : tr("hbui.PlayScreen.onlinePlayWarning.multiplayerGeneric.body", "During online play you may be exposed to chat messages or other types of user generated content that has not been rated, and may not be suitable for all ages.");
    float textWidth = std::min(ModalWidth, width - 16.0f) - 18.0f;
    float textHeight = ui.paragraphHeight(heading, TextStyle::Ui, textWidth) + css(30.0f) + ui.paragraphHeight(body, TextStyle::UiSmall, textWidth);
    float bodyHeight = std::max(WarningBodyHeight, textHeight);
    Rect content = modalFrame(ui, width, height, ModalWidth, ModalChrome + bodyHeight + 8.0f + ButtonHeight, tr("hbui.PlayScreen.onlinePlayWarning.title", "Play"), closed);
    if (closed) {
        warnedRow.reset();
        return;
    }
    float y = content.y + ui.paragraph(heading, TextStyle::Ui, content.x, content.y, content.w, White) + css(30.0f);
    ui.paragraph(body, TextStyle::UiSmall, content.x, y, content.w, Muted0);
    Rect left { content.x, content.bottom() - ButtonHeight, std::floor(content.w * 0.5f) - 2.0f, ButtonHeight };
    Rect right { left.right() + 4.0f, left.y, content.w - left.w - 4.0f, ButtonHeight };
    if (ui.pressableButton("warning:back", "pressableElevatedSecondary", tr("hbui.PlayScreen.onlinePlayWarning.back", "Back"), left)) {
        warnedRow.reset();
        closed = true;
        return;
    }
    if (ui.pressableButton("warning:proceed", "pressableElevatedPrimary", tr("hbui.PlayScreen.onlinePlayWarning.proceed", "Proceed"), right)) {
        setExtraOption(std::string(IpSafetyWarningOption), 1);
        setExtraOption(std::string(OnlineSafetyWarningOption), 1);
        ServerRow row = std::move(*warnedRow);
        warnedRow.reset();
        closed = true;
        connect(row);
    }
}

/**
 * Leaving the form with changes: editing asks to save or discard them, adding
 * asks to stay or go back without saving, each with the game's texts.
 */
void Menu::discardServerChangesDialog(Context& ui, float width, float height, bool& closed)
{
    std::string prefix = editing ? "hbui.PlayScreen.serverTab.unsavedChanges" : "hbui.PlayScreen.serverTab.unsavedAddChanges";
    std::string heading = editing ? tr(prefix + ".title", "Do you want to save your changes?") : tr(prefix + ".title", "Are you sure?");
    std::string body = editing ? tr(prefix + ".message", "You have unsaved changes. Make sure to save or discard your changes.")
                               : tr(prefix + ".message", "If you go back, any changes you made will not be saved.");
    std::string discard = editing ? tr(prefix + ".discardChangesButton", "Discard changes") : tr(prefix + ".discardChangesButton", "Go back without saving");
    std::string keep = editing ? tr(prefix + ".saveChangesButton", "Save changes") : tr(prefix + ".saveChangesButton", "Stay here");
    bool discardPressed = false;
    bool keepPressed = false;
    serverFormModal(ui, width, height, heading, body, discard, keep, "pressableElevatedPrimary", discardPressed, keepPressed, closed);
    if (discardPressed) {
        closed = true;
        navigate(Screen::Play);
        screenDirection = -1.0f;
        return;
    }
    if (!keepPressed) {
        return;
    }
    if (!editing) {
        closed = true;
        return;
    }
    dialog = Dialog::None;
    saveServerForm(false);
}

}
