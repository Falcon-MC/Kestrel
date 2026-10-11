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

constexpr float FormMaxWidth = 700.0f;
constexpr float SideMinWidth = 120.0f;
constexpr float SideMaxWidth = 190.0f;
constexpr float SideTabHeight = 26.0f;
constexpr float Gap = 4.0f;
constexpr float Padding = 12.0f;
constexpr float FieldHeight = 22.67f;
constexpr float RowGap = 10.0f;
constexpr float ToggleWidth = 30.0f;
constexpr float ToggleHeight = 16.0f;

enum Tab {
    General,
    Advanced,
    Cheats,
};

struct Choice {
    int value;
    std::string label;
};

/**
 * A row of buttons where the chosen one stands out, the way the game lays out
 * its game mode and difficulty pickers.
 */
void choiceRow(Context& ui, std::string_view id, const std::vector<Choice>& choices, int& value, float x, float y, float width)
{
    float buttonWidth = std::floor((width - Gap * static_cast<float>(choices.size() - 1)) / static_cast<float>(choices.size()));
    for (size_t i = 0; i < choices.size(); ++i) {
        Rect button { x + static_cast<float>(i) * (buttonWidth + Gap), y, buttonWidth, ButtonHeight };
        std::string key = std::string(id) + ":" + std::to_string(choices[i].value);
        bool chosen = choices[i].value == value;
        if (ui.pressableButton(key, chosen ? "pressableElevatedPrimary" : "pressableElevatedSecondary", choices[i].label, button) && !chosen) {
            value = choices[i].value;
        }
    }
}

std::vector<Choice> gameModes()
{
    return {
        { 0, tr("hbui.CreateNewWorld.general.gameModeSurvivalLabel", "Survival") },
        { 1, tr("hbui.CreateNewWorld.general.gameModeCreativeLabel", "Creative") },
        { 2, tr("hbui.CreateNewWorld.general.gameModeAdventureLabel", "Adventure") },
    };
}

std::vector<Choice> difficulties()
{
    return {
        { 0, tr("hbui.CreateNewWorld.general.difficultyPeacefulLabel", "Peaceful") },
        { 1, tr("hbui.CreateNewWorld.general.difficultyEasyLabel", "Easy") },
        { 2, tr("hbui.CreateNewWorld.general.difficultyNormalLabel", "Normal") },
        { 3, tr("hbui.CreateNewWorld.general.difficultyHardLabel", "Hard") },
    };
}

std::string difficultyDescription(int difficulty)
{
    switch (difficulty) {
    case 0:
        return tr("hbui.CreateNewWorld.general.difficultyPeacefulDescription", "No hostile mobs and only some neutral mobs spawn. Hunger bar doesn't deplete and health replenishes over time.");
    case 1:
        return tr("hbui.CreateNewWorld.general.difficultyEasyDescription", "Hostile mobs spawn but deal less damage. Hunger bar depletes and drains health down to 5 hearts.");
    case 3:
        return tr("hbui.CreateNewWorld.general.difficultyHardDescription", "Hostile mobs spawn and deal more damage. Hunger bar depletes and drains all health.");
    default:
        return tr("hbui.CreateNewWorld.general.difficultyNormalDescription", "Hostile mobs spawn and deal standard damage. Hunger bar depletes and drains health down to half a heart.");
    }
}

std::string gameModeDescription(int gameType)
{
    switch (gameType) {
    case 1:
        return tr("hbui.CreateNewWorld.general.gameModeCreativeDescription", "Create, build, and explore without limits. You can fly, have endless materials, and can't be hurt by monsters.");
    case 2:
        return tr("hbui.CreateNewWorld.general.gameModeAdventureDescription", "You get to set your own rules through in-game commands on how you and other can interact with the game.");
    default:
        return tr("hbui.CreateNewWorld.general.gameModeSurvivalDescription", "Explore a mysterious world where you build, collect, craft, and fight monsters.");
    }
}

}

void Menu::showWorldProgress(std::string heading)
{
    worldProgress = std::move(heading);
    if (dialog == Dialog::None) {
        dialog = Dialog::WorldProgress;
        field = Field::None;
    }
}

void Menu::hideWorldProgress()
{
    if (!worldProgress) {
        return;
    }
    worldProgress.reset();
    if (dialog == Dialog::WorldProgress) {
        dialog = Dialog::None;
        playTab = PlayTab::Worlds;
        navigate(Screen::Play);
    }
}

void Menu::openWorldForm(const LocalWorld* world)
{
    if (world) {
        editingWorld = world->id;
        worldDraft = world->settings;
    } else {
        editingWorld.reset();
        worldDraft = WorldSettings {};
    }
    worldFormTab = General;
    navigate(Screen::WorldForm);
    field = Field::WorldName;
}

/**
 * The create and edit world screens as the game splits them: a side menu of
 * tabs, General, Advanced and Cheats, with the Create or Play button under
 * them, and the chosen tab's settings beside it.
 */
void Menu::worldForm(Context& ui, float width, float height)
{
    const LocalWorld* edited = nullptr;
    if (editingWorld) {
        for (const LocalWorld& world : localWorlds) {
            if (world.id == *editingWorld) {
                edited = &world;
            }
        }
        if (!edited) {
            navigate(Screen::Play);
            return;
        }
    }
    float top = header(ui, width, edited ? tr("hbui.CreateNewWorld.editTitle", "Edit world") : tr("hbui.CreateNewWorld.title", "Create New World"), false);
    float formWidth = std::min(FormMaxWidth, width - 16.0f);
    Rect form { std::round((width - formWidth) * 0.5f), top + 8.0f, formWidth, std::max(1.0f, height - top - 16.0f) };
    float sideWidth = std::clamp(std::round(form.w * 0.3f), SideMinWidth, SideMaxWidth);
    Rect side { form.x, form.y, sideWidth, form.h };
    Rect view { side.right() + Gap * 2.0f, form.y, form.right() - side.right() - Gap * 2.0f, form.h };

    ui.fill(side, PanelDark);
    struct TabEntry {
        Tab tab;
        std::string label;
    };
    TabEntry tabs[] = {
        { General, tr("hbui.CreateNewWorldSideMenu.generalTabLabel", "General") },
        { Advanced, tr("hbui.CreateNewWorldSideMenu.advancedTabLabel", "Advanced") },
        { Cheats, tr("hbui.CreateNewWorldSideMenu.cheatsTabLabel", "Cheats") },
    };
    float tabY = side.y + Gap;
    for (const TabEntry& entry : tabs) {
        Rect tab { side.x + Gap, tabY, side.w - Gap * 2.0f, SideTabHeight };
        bool active = worldFormTab == entry.tab;
        if (ui.pressableButton(std::string("world:tab:") + std::to_string(entry.tab), active ? "pressableElevatedPrimary" : "pressableElevatedSecondary", entry.label, tab) && !active) {
            worldFormTab = entry.tab;
            pageScroll = 0.0f;
            field = Field::None;
        }
        tabY += SideTabHeight + Gap;
    }

    WorldSettings settings = worldDraft;
    if (settings.name.empty()) {
        settings.name = edited ? edited->settings.name : tr("hbui.CreateNewWorld.general.worldNamePlaceholder", "My World");
    }
    Rect action { side.x + Gap, side.bottom() - Gap - ButtonHeight, side.w - Gap * 2.0f, ButtonHeight };
    if (edited) {
        if (ui.pressableButton("world:play", "pressableElevatedPrimary", tr("hbui.WorldCard.play", "Play"), action)) {
            if (!(settings == edited->settings)) {
                worldRequests.push_back(WorldRequest { WorldRequest::Kind::Save, edited->id, settings });
            }
            worldRequests.push_back(WorldRequest { WorldRequest::Kind::Play, edited->id, {} });
            navigate(Screen::Play);
            return;
        }
    } else if (ui.pressableButton("world:create", "pressableElevatedPrimary", tr("hbui.CreateNewWorldSideMenu.create", "Create"), action)) {
        worldRequests.push_back(WorldRequest { WorldRequest::Kind::Create, {}, settings });
        navigate(Screen::Play);
        return;
    }

    float textWidth = view.w - Padding * 2.0f;
    float lineHeight = ui.lineHeight(TextStyle::Ui);
    std::string modeText = gameModeDescription(worldDraft.gameType);
    std::string difficultyText = difficultyDescription(worldDraft.difficulty);
    std::string seedText = tr("hbui.CreateNewWorld.advanced.worldSeedDescription", "Guides the algorithm that magically creates your world");

    float content = Padding;
    switch (worldFormTab) {
    case General:
        content += lineHeight + 4.0f + FieldHeight + RowGap;
        content += lineHeight + 4.0f + ButtonHeight + 4.0f + ui.paragraphHeight(modeText, TextStyle::Ui, textWidth) + RowGap;
        content += lineHeight + 4.0f + ButtonHeight + 4.0f + ui.paragraphHeight(difficultyText, TextStyle::Ui, textWidth) + RowGap;
        if (edited) {
            content += lineHeight + 4.0f + (ButtonHeight + 4.0f) * 3.0f + RowGap;
        }
        break;
    case Advanced:
        content += lineHeight + 4.0f + ui.paragraphHeight(seedText, TextStyle::Ui, textWidth) + 4.0f + FieldHeight + RowGap;
        if (!edited) {
            content += lineHeight * 2.0f + RowGap;
        }
        break;
    default:
        content += lineHeight * 2.0f + RowGap;
        break;
    }

    scrollArea(ui, view, pageScroll, content);
    ui.fill({ view.x, view.y, view.w, std::min(view.h, content) }, Panel);
    ui.setClip(view);
    float x = view.x + Padding;
    float y = view.y + Padding - pageScroll;

    auto toggleRow = [&](std::string_view id, const std::string& title, const std::string& description, bool& value) {
        ui.text(title, TextStyle::Ui, x, y, White, textWidth - ToggleWidth - 8.0f);
        ui.text(description, TextStyle::Ui, x, y + lineHeight, Muted1, textWidth - ToggleWidth - 8.0f);
        if (toggle(ui, id, { x + textWidth - ToggleWidth, y + lineHeight - ToggleHeight * 0.5f, ToggleWidth, ToggleHeight }, value)) {
            value = !value;
        }
        y += lineHeight * 2.0f + RowGap;
    };

    switch (worldFormTab) {
    case General:
        ui.text(tr("hbui.CreateNewWorld.general.worldNameTitle", "World name"), TextStyle::Ui, x, y, White);
        y += lineHeight + 4.0f;
        if (textField(ui, "world:name", tr("hbui.CreateNewWorld.general.worldNamePlaceholder", "My World"), worldDraft.name, { x, y, textWidth, FieldHeight }, field == Field::WorldName)) {
            field = Field::WorldName;
        }
        y += FieldHeight + RowGap;

        ui.text(tr("hbui.CreateNewWorld.general.gameModeTitle", "Game mode"), TextStyle::Ui, x, y, White);
        y += lineHeight + 4.0f;
        choiceRow(ui, "world:mode", gameModes(), worldDraft.gameType, x, y, textWidth);
        y += ButtonHeight + 4.0f;
        ui.paragraph(modeText, TextStyle::Ui, x, y, textWidth, Muted1);
        y += ui.paragraphHeight(modeText, TextStyle::Ui, textWidth) + RowGap;

        ui.text(tr("hbui.CreateNewWorld.general.difficultyTitle", "Difficulty"), TextStyle::Ui, x, y, White);
        y += lineHeight + 4.0f;
        choiceRow(ui, "world:difficulty", difficulties(), worldDraft.difficulty, x, y, textWidth);
        y += ButtonHeight + 4.0f;
        ui.paragraph(difficultyText, TextStyle::Ui, x, y, textWidth, Muted1);
        y += ui.paragraphHeight(difficultyText, TextStyle::Ui, textWidth) + RowGap;

        if (edited) {
            ui.text(tr("hbui.CreateNewWorld.general.fileManagementTitle", "File management"), TextStyle::Ui, x, y, White);
            y += lineHeight + 4.0f;
            if (ui.pressableButton("world:duplicate", "pressableElevatedSecondary", tr("hbui.CreateNewWorld.general.fileManagementDuplicateWorldLabel", "Duplicate world"), { x, y, textWidth, ButtonHeight })) {
                worldRequests.push_back(WorldRequest { WorldRequest::Kind::Duplicate, edited->id, {} });
            }
            y += ButtonHeight + 4.0f;
            if (ui.pressableButton("world:export", "pressableElevatedSecondary", tr("hbui.CreateNewWorld.general.fileManagementExportWorldLabel", "Export world"), { x, y, textWidth, ButtonHeight })) {
                worldRequests.push_back(WorldRequest { WorldRequest::Kind::Export, edited->id, {} });
            }
            y += ButtonHeight + 4.0f;
            if (ui.pressableButton("world:delete", "pressableElevatedDestructive", tr("hbui.CreateNewWorld.general.fileManagementDeleteWorldLabel", "Delete world"), { x, y, textWidth, ButtonHeight })) {
                dialog = Dialog::ConfirmDeleteWorld;
            }
        }
        break;
    case Advanced:
        ui.text(tr("hbui.CreateNewWorld.advanced.worldSeedLabel", "World seed"), TextStyle::Ui, x, y, White);
        y += lineHeight + 4.0f;
        ui.paragraph(seedText, TextStyle::Ui, x, y, textWidth, Muted1);
        y += ui.paragraphHeight(seedText, TextStyle::Ui, textWidth) + 4.0f;
        if (edited) {
            ui.fill({ x, y, textWidth, FieldHeight }, PanelDark);
            ui.text(edited->settings.seed, TextStyle::Ui, x + 4.0f, std::round(y + (FieldHeight - lineHeight) * 0.5f), Muted0, textWidth - 8.0f);
        } else if (textField(ui, "world:seed", tr("hbui.CreateNewWorld.advanced.worldSeedPlaceholder", "3257840388504953787"), worldDraft.seed, { x, y, textWidth, FieldHeight }, field == Field::WorldSeed)) {
            field = Field::WorldSeed;
        }
        y += FieldHeight + RowGap;
        if (!edited) {
            toggleRow("world:bonus", tr("hbui.CreateNewWorld.advanced.bonusChestTitle", "Bonus chest"),
                tr("hbui.CreateNewWorld.advanced.bonusChestDescription", "Spawn close to a chest with items to get you started"), worldDraft.bonusChest);
        }
        break;
    default:
        toggleRow("world:cheats", tr("hbui.CreateNewWorld.cheats.title", "Cheats"),
            tr("hbui.CreateNewWorld.cheats.description", "Customize the game with cheat settings"), worldDraft.cheats);
        break;
    }
    ui.clearClip();
}

void Menu::deleteWorldDialog(Context& ui, float width, float height, bool& closed)
{
    bool keep = false;
    bool remove = false;
    serverFormModal(ui, width, height, tr("hbui.CreateNewWorld.deleteWorldModal.title", "Are you sure?"),
        tr("hbui.CreateNewWorld.deleteWorldModal.message", "If you delete this world it will be gone forever."),
        tr("hbui.CreateNewWorld.deleteWorldModal.continueButton", "Continue editing"), tr("hbui.CreateNewWorld.deleteWorldModal.deleteButton", "Delete world"),
        "pressableElevatedDestructive", keep, remove, closed);
    if (keep) {
        closed = true;
    }
    if (remove && editingWorld) {
        worldRequests.push_back(WorldRequest { WorldRequest::Kind::Delete, *editingWorld, {} });
        dialog = Dialog::None;
        navigate(Screen::Play);
        screenDirection = -1.0f;
    }
}

}
