#include "menu/Menu.h"
#include "menu/PlayLayout.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;
using namespace play;

namespace {

constexpr float Spacing2 = 0.8f * Rem;
constexpr float Spacing4 = 1.6f * Rem;
constexpr float HeaderButtonWidth = 25.6f * Rem;
constexpr float LayoutButtonSize = 4.4f * Rem;
constexpr float ActionBarHeight = 6.8f * Rem;
constexpr float CardGap = 1.6f * Rem;
constexpr float PlayerIcon = 2.4f * Rem;

/**
 * The game picks how many world cards fit side by side from the width left
 * for them, in rem: four, three, two, or a single column shown as a list.
 */
int worldColumns(float width)
{
    auto span = [](int columns) {
        return (25.0f * static_cast<float>(columns) + 1.6f * static_cast<float>(columns - 1)) * Rem;
    };
    float room = std::max(0.0f, width - 0.8f * Rem);
    if (room >= span(4)) {
        return 4;
    }
    if (room >= span(3)) {
        return 3;
    }
    if (room >= span(2) + 12.5f * Rem) {
        return 2;
    }
    return 1;
}

std::string worldTitle(const SocialPerson& person)
{
    return person.worldName.empty() ? person.gamertag : person.worldName;
}

/**
 * A world tag: a filled label in its theme role, the way the game marks a
 * friend's world on its card.
 */
float worldTag(Context& ui, std::string_view label, float x, float y, Color background)
{
    float width = ui.measure(label, TextStyle::UiSmall) + 6.0f;
    float height = ui.lineHeight(TextStyle::UiSmall) + 3.0f;
    ui.fill({ x, y, width, height }, background);
    ui.text(label, TextStyle::UiSmall, x + 3.0f, y + 2.0f, InkDark);
    return width;
}

/**
 * The "players / capacity" counter on a joinable world, hidden while nobody is
 * in it, with the full variant of the icon once it has no room left.
 */
void playerCount(Context& ui, const SocialPerson& person, float right, float centerY)
{
    if (person.members <= 0) {
        return;
    }
    std::string count = std::to_string(person.members) + "/" + std::to_string(person.maxMembers);
    float textWidth = ui.measure(count, TextStyle::UiSmall);
    float x = right - textWidth - PlayerIcon - 0.4f * Rem;
    bool full = person.maxMembers > 0 && person.members >= person.maxMembers;
    ui.sprite({ x, std::round(centerY - PlayerIcon * 0.5f), PlayerIcon, PlayerIcon }, full ? "hbui/player-online-icon-full" : "hbui/player-online-icon");
    ui.text(count, TextStyle::UiSmall, x + PlayerIcon + 0.4f * Rem, std::round(centerY - ui.lineHeight(TextStyle::UiSmall) * 0.5f), White);
}

}

std::vector<const SocialPerson*> Menu::friendWorlds() const
{
    std::vector<const SocialPerson*> worlds;
    for (const SocialPerson& person : social.friends.people) {
        if (joinable(person)) {
            worlds.push_back(&person);
        }
    }
    return worlds;
}

void Menu::worldCard(Context& ui, const SocialPerson& person, const Rect& card)
{
    Rect picture { card.x, card.y, card.w, std::round(card.w * 9.0f / 16.0f) };
    Interaction pictureState = ui.interact("world:image:" + person.xuid, picture);
    ui.fill(picture, Neutral100);
    ui.sprite(picture.inset(1.0f), "hbui/world-preview-default");
    std::string tag = tr("hbui.WorldTag.friend", "Friend\xE2\x80\x99s world");
    float tagHeight = ui.lineHeight(TextStyle::UiSmall) + 3.0f;
    float tagWidth = ui.measure(tag, TextStyle::UiSmall) + 6.0f;
    ui.fill({ picture.x, picture.y, tagWidth + 1.0f, tagHeight + 1.0f }, { 0, 0, 0, 178 });
    worldTag(ui, tag, picture.x, picture.y, SuccessTint);

    Rect action { card.x, picture.bottom(), card.w, ActionBarHeight };
    Interaction actionState = ui.pressable("world:play:" + person.xuid, "detailedCardAction", action);
    float textX = action.x + Spacing2 * 2.0f;
    float countWidth = person.members > 0 ? ui.measure(std::to_string(person.members) + "/" + std::to_string(person.maxMembers), TextStyle::UiSmall) + PlayerIcon + 0.4f * Rem : 0.0f;
    float textWidth = std::max(1.0f, action.right() - textX - countWidth - Spacing2 * 3.0f);
    float lines = ui.lineHeight(TextStyle::Ui) * 2.0f;
    float textY = std::round(action.y + (action.h - css(4.0f) - lines) * 0.5f);
    ui.text(worldTitle(person), TextStyle::Ui, textX, textY, White, textWidth);
    ui.text(trf("hbui.WorldGrid.lanWorldOwner", "%1$s's world", { person.gamertag }), TextStyle::Ui, textX, textY + ui.lineHeight(TextStyle::Ui), Muted0, textWidth);
    playerCount(ui, person, action.right() - Spacing2 * 2.0f, action.y + (action.h - css(4.0f)) * 0.5f);
    if (pictureState.clicked || actionState.clicked) {
        pending = ConnectRequest { worldTitle(person), "session_handle/" + person.sessionHandle };
    }
}

void Menu::worldListRow(Context& ui, const SocialPerson& person, const Rect& row)
{
    Interaction state = ui.pressable("world:row:" + person.xuid, "listItemAction", row);
    Rect thumbnail { row.x + Spacing2, row.y + Spacing2, std::round((row.h - Spacing2 * 2.0f - css(4.0f)) * 16.0f / 9.0f), row.h - Spacing2 * 2.0f - css(4.0f) };
    ui.fill(thumbnail, Neutral100);
    ui.sprite(thumbnail.inset(1.0f), "hbui/world-preview-default");
    float textX = thumbnail.right() + Spacing2 * 2.0f;
    float textWidth = std::max(1.0f, row.right() - textX - 48.0f);
    float lines = ui.lineHeight(TextStyle::Ui) * 2.0f;
    float textY = std::round(row.y + (row.h - css(4.0f) - lines) * 0.5f);
    ui.text(worldTitle(person), TextStyle::Ui, textX, textY, White, textWidth);
    ui.text(trf("hbui.WorldList.lanWorldOwner", "%1$s's world", { person.gamertag }), TextStyle::Ui, textX, textY + ui.lineHeight(TextStyle::Ui), Muted0, textWidth);
    playerCount(ui, person, row.right() - Spacing2 * 2.0f, row.y + (row.h - css(4.0f)) * 0.5f);
    if (state.clicked) {
        pending = ConnectRequest { worldTitle(person), "session_handle/" + person.sessionHandle };
    }
}

/**
 * Kestrel keeps no worlds of its own, so the tab only ever lists the worlds
 * friends are hosting. Creating or importing a world needs the local game
 * server Kestrel does not have, so those buttons stay disabled.
 */
void Menu::worldsTab(Context& ui, const Rect& area)
{
    std::vector<const SocialPerson*> worlds = friendWorlds();
    int columns = worldColumns(area.w);
    bool list = columns == 1 || worldsListLayout;
    float y = area.y + Spacing2;
    float headerBottom = y + ButtonHeight + Spacing2;

    Rect empty { area.x, headerBottom + Spacing2, area.w, area.bottom() - headerBottom - Spacing2 };
    bool large = empty.w >= 70.0f * Rem;
    float artWidth = large ? 224.0f : 112.0f;
    float artHeight = large ? 96.0f : 48.0f;
    std::string emptyText = tr("hbui.PlayScreen.allWorlds.emptyStateText", "Create a new world from scratch or create a world from a template.");
    float emptyTextWidth = std::min(empty.w, 100.0f * Rem);
    std::vector<std::string_view> emptyLines;
    float cardWidth = std::floor((area.w - 4.0f + CardGap) / static_cast<float>(columns) - CardGap);
    float cardHeight = std::round(cardWidth * 9.0f / 16.0f) + ActionBarHeight;
    float content = 0.0f;
    float backgroundBottom = 0.0f;
    if (worlds.empty()) {
        ui.wrap(emptyText, TextStyle::Ui, emptyTextWidth, emptyLines);
        backgroundBottom = empty.y + Spacing4 * 2.0f + artHeight + ui.lineHeight(TextStyle::HeadingSmall) + Spacing2 + static_cast<float>(emptyLines.size()) * ui.lineHeight(TextStyle::Ui) + Spacing4;
    } else {
        if (list) {
            content = static_cast<float>(worlds.size()) * (ActionBarHeight + 0.4f * Rem);
        } else {
            size_t rows = (worlds.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns);
            content = static_cast<float>(rows) * (cardHeight + CardGap);
        }
        backgroundBottom = headerBottom + content + Spacing2;
    }
    ui.fill({ area.x, area.y, area.w, std::min(area.bottom(), backgroundBottom) - area.y }, NeutralAlpha60);

    float buttonWidth = std::min(HeaderButtonWidth, (area.w - Spacing4 - (worlds.empty() ? 0.0f : LayoutButtonSize + Spacing4)) * 0.5f);
    Rect create { area.right() - buttonWidth, y, buttonWidth, ButtonHeight };
    Rect fromTemplate { create.x - Spacing4 - buttonWidth, y, buttonWidth, ButtonHeight };
    ui.pressableButton("worlds:template", "pressableElevatedSecondary", tr("hbui.PlayScreen.AllWorldsTab.ButtonHeader.createFromTemplate", "Create from template"), fromTemplate, TextStyle::Ui, false);
    ui.pressableButton("worlds:create", "pressableElevatedPrimary", tr("hbui.PlayScreen.AllWorldsTab.ButtonHeader.createNewWorld", "Create new world"), create, TextStyle::Ui, false);
    if (!worlds.empty() && columns > 1) {
        Rect layout { area.x, y + ButtonHeight - LayoutButtonSize - css(4.0f), LayoutButtonSize + css(4.0f), LayoutButtonSize + css(4.0f) };
        Interaction state = ui.pressable("worlds:layout", "pressableElevatedNeutral", layout);
        Rect face { layout.x, layout.y, layout.w, layout.h - (state.pressed ? 0.0f : css(4.0f)) };
        float icon = css(24.0f);
        ui.sprite({ std::round(face.x + (face.w - icon) * 0.5f), std::round(face.y + (face.h - icon) * 0.5f), icon, icon }, worldsListLayout ? "hbui/List" : "hbui/Grid", InkDark);
        if (state.clicked) {
            worldsListLayout = !worldsListLayout;
            listScroll = 0.0f;
        }
    }
    y = headerBottom;

    if (worlds.empty()) {
        float top = empty.y + Spacing4;
        ui.sprite({ std::round(empty.x + (empty.w - artWidth) * 0.5f), top, artWidth, artHeight }, "hbui/no-worlds-yet-art");
        top += artHeight + Spacing4;
        ui.textCentered(tr("hbui.PlayScreen.allWorlds.emptyStateTitle", "No worlds here... yet!"), TextStyle::HeadingSmall, { empty.x, top, empty.w, ui.lineHeight(TextStyle::HeadingSmall) }, White);
        top += ui.lineHeight(TextStyle::HeadingSmall) + Spacing2;
        float textX = std::round(empty.x + (empty.w - emptyTextWidth) * 0.5f);
        for (std::string_view line : emptyLines) {
            ui.textCentered(line, TextStyle::Ui, { textX, top, emptyTextWidth, ui.lineHeight(TextStyle::Ui) }, Muted0);
            top += ui.lineHeight(TextStyle::Ui);
        }
        return;
    }

    Rect view { area.x, y, area.w, area.bottom() - y };
    scrollArea(ui, view, listScroll, content);
    ui.setClip(view);
    for (size_t i = 0; i < worlds.size(); ++i) {
        if (list) {
            Rect row { view.x, view.y - listScroll + static_cast<float>(i) * (ActionBarHeight + 0.4f * Rem), view.w - 4.0f, ActionBarHeight };
            if (row.bottom() >= view.y && row.y <= view.bottom()) {
                worldListRow(ui, *worlds[i], row);
            }
            continue;
        }
        float column = static_cast<float>(i % static_cast<size_t>(columns));
        float rowIndex = static_cast<float>(i / static_cast<size_t>(columns));
        Rect card { std::round(view.x + column * (cardWidth + CardGap)), view.y - listScroll + rowIndex * (cardHeight + CardGap), cardWidth, cardHeight };
        if (card.bottom() >= view.y && card.y <= view.bottom()) {
            worldCard(ui, *worlds[i], card);
        }
    }
    ui.clearClip();
}

}
