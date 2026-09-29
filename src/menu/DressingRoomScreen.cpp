#include "menu/Menu.h"

#include "platform/Input.h"
#include "platform/Shell.h"
#include "util/SkinChoice.h"

#include <filesystem>
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr float HeaderHeight = 22.0f;
constexpr float SidebarWidth = 29.0f;
constexpr float SidebarOpenWidth = 141.0f;
constexpr float SidebarRowTop = 22.0f;
constexpr float SidebarRowHeight = 23.0f;
constexpr float TileSize = 62.0f;
constexpr float TileStep = 64.0f;
constexpr int TileColumns = 6;
constexpr float RowHeight = 25.0f;
constexpr const char* CoinPage = "MultiItemPage_DressingRoomCoinScreen";

constexpr Color HeaderFill { 198, 198, 198, 255 };
constexpr Color HeaderShadow { 38, 38, 39, 255 };
constexpr Color HeaderInk { 64, 64, 64, 255 };
constexpr Color SidebarFill { 49, 50, 51, 255 };
constexpr Color SidebarSelected { 72, 73, 74, 255 };
constexpr Color SidebarOpenTop { 198, 198, 198, 255 };
constexpr Color PanelFill { 32, 32, 33, 255 };
constexpr Color PanelEdge { 58, 58, 59, 255 };
constexpr Color InfoTitleFill { 58, 58, 59, 255 };
constexpr Color InfoFill { 35, 35, 36, 255 };
constexpr Color RowLine { 72, 72, 73, 255 };
constexpr Color CoinBox { 48, 48, 49, 255 };
constexpr Color Dim { 0, 0, 0, 150 };

/**
 * The hair palette of the character creator's color picker, row by row.
 */
constexpr std::array<Color, 29> HairColors { {
    { 234, 229, 247, 255 }, { 237, 224, 187, 255 }, { 247, 223, 139, 255 }, { 223, 190, 127, 255 }, { 200, 151, 50, 255 }, { 174, 128, 79, 255 },
    { 234, 153, 69, 255 }, { 232, 143, 40, 255 }, { 139, 100, 147, 255 }, { 161, 89, 14, 255 }, { 147, 65, 32, 255 }, { 112, 40, 0, 255 },
    { 69, 21, 0, 255 }, { 115, 73, 38, 255 }, { 99, 70, 43, 255 }, { 69, 41, 20, 255 }, { 47, 24, 14, 255 }, { 50, 51, 52, 255 },
    { 41, 26, 28, 255 }, { 29, 40, 51, 255 }, { 38, 26, 41, 255 }, { 27, 19, 16, 255 }, { 99, 103, 116, 255 }, { 154, 156, 165, 255 },
    { 234, 142, 174, 255 }, { 190, 71, 178, 255 }, { 122, 41, 170, 255 }, { 55, 58, 152, 255 }, { 57, 175, 219, 255 },
} };

struct Category {
    const char* page;
    const char* key;
    const char* fallback;
    const char* icon;
    bool colorable;
};

constexpr Category BodyCategories[] = {
    { "Bases", "dr.categories.base", "Bases", "ui/subcategory_icons/skin_texture", true },
    { "Hairs", "dr.categories.hair", "Hair", "ui/subcategory_icons/hair", true },
    { "Eyes", "dr.categories.eyes", "Eyes", "ui/subcategory_icons/eyes", true },
    { "Mouths", "dr.categories.mouth", "Mouths", "ui/subcategory_icons/mouth", true },
    { "FacialHairs", "dr.categories.facial_hair", "Facial Hair", "ui/subcategory_icons/facial_hair", true },
    { "Arms", "dr.categories.arms", "Arms", "ui/subcategory_icons/arms", false },
    { "Legs", "dr.categories.legs", "Legs", "ui/subcategory_icons/legs", false },
    { "Size", "dr.categories.size", "Size", "ui/subcategory_icons/body_size", false },
};

constexpr Category StyleCategories[] = {
    { "Tops", "dr.categories.top", "Tops", "ui/subcategory_icons/tops", false },
    { "Bottoms", "dr.categories.bottom", "Bottoms", "ui/subcategory_icons/bottoms", false },
    { "Outerwears", "dr.categories.outerwear", "Outerwear", "ui/subcategory_icons/outerwear", false },
    { "Headwears", "dr.categories.headwear", "Headwear", "ui/subcategory_icons/head", false },
    { "Gloves", "dr.categories.gloves", "Gloves", "ui/subcategory_icons/hands", false },
    { "Footwears", "dr.categories.footwear", "Footwear", "ui/subcategory_icons/feet", false },
    { "FaceItems", "dr.categories.face_item", "Face Items", "ui/subcategory_icons/face_accessory", false },
    { "BackItems", "dr.categories.back_item", "Back Items", "ui/subcategory_icons/back_accessory", false },
};

struct SidebarEntry {
    DressingSection section;
    const char* key;
    const char* fallback;
    const char* icon;
};

constexpr SidebarEntry SidebarEntries[] = {
    { DressingSection::Characters, "sidebar.myCharacters", "My Characters", "ui/sidebar_icons/my_characters" },
    { DressingSection::Creator, "sidebar.characterCreator", "Character Creator", "ui/sidebar_icons/character_creator" },
    { DressingSection::ClassicSkins, "sidebar.classicSkins", "Classic Skins", "ui/sidebar_icons/classic_skins" },
    { DressingSection::Emotes, "sidebar.emotes", "Emotes", "ui/sidebar_icons/emotes" },
    { DressingSection::Capes, "sidebar.capes", "Capes", "ui/sidebar_icons/capes" },
};

Color rarityColor(const std::string& rarity)
{
    if (rarity == "uncommon") {
        return { 71, 139, 76, 255 };
    }
    if (rarity == "rare") {
        return { 28, 50, 138, 255 };
    }
    if (rarity == "epic") {
        return { 87, 38, 111, 255 };
    }
    if (rarity == "legendary") {
        return { 186, 108, 22, 255 };
    }
    return { 108, 108, 108, 255 };
}

std::string upper(std::string text)
{
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

/**
 * Text cut with an ellipsis to fit a width, the way the sidebar shortens
 * labels that run past it.
 */
std::string fitted(const Context& ui, const std::string& text, TextStyle style, float width)
{
    if (ui.measure(text, style) <= width) {
        return text;
    }
    std::string cut = text;
    while (!cut.empty() && ui.measure(cut + "...", style) > width) {
        cut.pop_back();
    }
    return cut + "...";
}

void panelFrame(Context& ui, const Rect& rect, Color fill, Color edge)
{
    ui.fill(rect, edge);
    ui.fill(rect.inset(1.0f), fill);
}

}

void Menu::setDressingRoom(DressingRoomView view)
{
    dressing = std::move(view);
}

std::vector<std::string> Menu::takeDressingRequests()
{
    return std::exchange(dressingRequests, {});
}

void Menu::openDressingSection(DressingSection section, const std::string& page, const std::string& title)
{
    DressingState& state = dressingState;
    if (state.section != section || state.page != page) {
        state.trail.push_back(state.section);
    }
    state.section = section;
    if (!page.empty()) {
        state.page = page;
        state.pageTitle = title;
    }
    if (section != DressingSection::Colors) {
        state.selected.clear();
    }
    state.color = -1;
    state.scroll = 0.0f;
    state.sidebarOpen = false;
    state.search.clear();
    std::string wanted = section == DressingSection::Emotes ? "Emotes" : section == DressingSection::Capes ? "Capes" : section == DressingSection::Coins ? CoinPage : state.page;
    if ((section == DressingSection::Emotes || section == DressingSection::Capes || section == DressingSection::Category || section == DressingSection::Coins) && !dressing.contains(wanted)) {
        dressingRequests.push_back(wanted);
    }
}

/**
 * The dressing room: the header with the Minecoin balance, the sidebar of
 * sections and the section itself over the dimmed backdrop, with the
 * dialogs on top.
 */
void Menu::dressingRoom(Context& ui, float width, float height)
{
    DressingState& state = dressingState;
    if (!skinChoiceLoaded) {
        skinChoiceLoaded = true;
        applySkinChoice(ui);
    }
    panorama(ui, width, height);
    ui.fill({ 0.0f, 0.0f, width, height }, { 0, 0, 0, 120 });

    float left = SidebarWidth;
    float contentWidth = width - left;
    std::string title = tr("profileScreen.header", "Dressing Room");
    bool search = false;
    switch (state.section) {
    case DressingSection::Characters:
    case DressingSection::Fullscreen:
        dressingCharacters(ui, left, contentWidth, height);
        break;
    case DressingSection::Coins:
        title = tr("store.coin.title", "Buy Minecoins");
        dressingCoins(ui, left, contentWidth, height);
        break;
    case DressingSection::Creator:
    case DressingSection::ClassicSkins:
    case DressingSection::Emotes:
    case DressingSection::Capes:
    case DressingSection::Category:
    case DressingSection::Colors: {
        search = state.section != DressingSection::Colors;
        float split = std::floor(left + (contentWidth - 4.0f) * 0.52f);
        Rect panel { left + 3.0f, HeaderHeight + 4.0f, split - left - 3.0f, height - HeaderHeight - 4.0f };
        panelFrame(ui, panel, PanelFill, PanelEdge);
        Rect preview { split + 2.0f, HeaderHeight, width - split - 2.0f, height - HeaderHeight };
        bool colorable = false;
        if (state.section == DressingSection::Creator) {
            title = tr("dr.header.customization", "Character Creator");
            dressingCreator(ui, panel);
        } else if (state.section == DressingSection::ClassicSkins) {
            title = tr("dr.header.classic_skins", "Classic Skins");
            dressingClassicSkins(ui, panel);
        } else if (state.section == DressingSection::Emotes) {
            title = tr("dr.header.animation", "Emotes");
            std::string noun = tr("dr.categories.emotes", "Emotes");
            dressingPieces(ui, panel, "Emotes", trf("dr.collector_title.owned", "Your %s", { noun }), trf("dr.collector_title.general", "All %s", { noun }), tr("dr.none_emote_button_text", "Clear All"));
        } else if (state.section == DressingSection::Capes) {
            title = tr("dr.header.capes", "Capes");
            dressingPieces(ui, panel, "Capes", trf("dr.collector_title.owned", "Your %s", { tr("dr.categories.capes", "Capes") }), "", tr("dr.none_button_text", "None"));
        } else if (state.section == DressingSection::Category) {
            title = tr("sidebar.marketplace", "Store");
            for (const Category& category : BodyCategories) {
                colorable |= state.page == category.page && category.colorable;
            }
            dressingPieces(ui, panel, state.page, trf("dr.collector_title.owned", "Your %s", { state.pageTitle }), trf("dr.collector_title.general", "All %s", { state.pageTitle }), tr("dr.none_button_text", "None"));
        } else {
            title = state.pageTitle;
            for (const auto& [id, page] : dressing) {
                for (const auto* list : { &page.owned, &page.others }) {
                    for (const DressingPiece& piece : *list) {
                        if (piece.id == state.selected) {
                            title = piece.title;
                        }
                    }
                }
            }
            dressingColors(ui, panel);
        }
        dressingPreview(ui, preview, colorable && !state.selected.empty());
        break;
    }
    }

    dressingHeader(ui, width, title, search);
    dressingSidebar(ui, width, height);
    dressingDialog(ui, width, height);
}

void Menu::dressingHeader(Context& ui, float width, std::string_view title, bool search)
{
    ui.fill({ 0.0f, 0.0f, width, HeaderHeight - 2.0f }, HeaderFill);
    ui.fill({ 0.0f, HeaderHeight - 2.0f, width, 2.0f }, HeaderShadow);
    Rect back { 0.0f, 0.0f, 24.0f, HeaderHeight - 2.0f };
    Interaction backState = ui.interact("dressing:back", back);
    ui.sprite({ 8.0f, 6.0f, 4.0f, 8.0f }, "hbui/arrowBack", backState.hovered ? White : HeaderInk);
    if (backState.clicked) {
        DressingState& state = dressingState;
        if (state.section == DressingSection::Colors || state.section == DressingSection::Fullscreen || !state.trail.empty()) {
            state.section = state.trail.empty() ? DressingSection::Characters : state.trail.back();
            if (!state.trail.empty()) {
                state.trail.pop_back();
            }
            state.selected.clear();
            state.scroll = 0.0f;
        } else {
            goBack();
        }
    }
    ui.text(title, TextStyle::Pixel, 27.0f, std::round((HeaderHeight - 2.0f - ui.lineHeight(TextStyle::Pixel)) * 0.5f), HeaderInk);

    float right = width - 2.0f;
    if (search) {
        Rect glass { right - 16.0f, 2.0f, 16.0f, 16.0f };
        if (ui.classicButton("dressing:search", "", glass)) {
            field = Field::DressingSearch;
        }
        ui.sprite({ glass.x + 3.0f, glass.y + 3.0f, 10.0f, 10.0f }, "hbui/search", HeaderInk);
        Rect box { glass.x - 81.0f, 2.0f, 80.0f, 16.0f };
        if (textField(ui, "dressing:search_field", tr("controller.buttonTip.enterSearch", "Search") + ".", dressingState.search, box, field == Field::DressingSearch)) {
            field = Field::DressingSearch;
            dressingState.scroll = 0.0f;
        }
        right = box.x - 5.0f;
    }
    Rect plus { right - 16.0f, 2.0f, 16.0f, 16.0f };
    if (ui.classicButton("dressing:coins", "+", plus)) {
        openDressingSection(DressingSection::Coins);
    }
    Rect box { plus.x - 46.0f, 2.0f, 46.0f, 16.0f };
    ui.fill(box, CoinBox);
    auto balancePage = dressing.find("__balance");
    if (balancePage == dressing.end() && std::find(dressingRequests.begin(), dressingRequests.end(), "__balance") == dressingRequests.end()) {
        dressingRequests.push_back("__balance");
    }
    std::string balance = balancePage != dressing.end() && balancePage->second.balance >= 0 ? std::to_string(balancePage->second.balance) : "0";
    float amountWidth = ui.measure(balance, TextStyle::Pixel);
    ui.text(balance, TextStyle::Pixel, box.x + box.w - amountWidth - 2.0f, box.y + 4.0f, { 255, 222, 60, 255 });
    ui.sprite({ box.x + box.w - amountWidth - 12.0f, box.y + 3.0f, 9.0f, 9.0f }, "ui/icon_minecoin_9x9");
}

/**
 * The section bar on the left: icons alone, or their labels over the dimmed
 * room once the menu button opens it, with the store button at the bottom.
 */
void Menu::dressingSidebar(Context& ui, float width, float height)
{
    DressingState& state = dressingState;
    float barWidth = state.sidebarOpen ? SidebarOpenWidth : SidebarWidth;
    if (state.sidebarOpen) {
        Interaction outside = ui.interact("dressing:sidebar_dim", { barWidth, HeaderHeight, width - barWidth, height - HeaderHeight });
        ui.fill({ barWidth, HeaderHeight, width - barWidth, height - HeaderHeight }, Dim);
        if (outside.clicked) {
            state.sidebarOpen = false;
        }
    }
    ui.fill({ 0.0f, HeaderHeight, barWidth, height - HeaderHeight }, SidebarFill);
    ui.fill({ barWidth - 1.0f, HeaderHeight, 1.0f, height - HeaderHeight }, Divider);

    Rect menuRow { 0.0f, SidebarRowTop, barWidth - 1.0f, SidebarRowHeight };
    Interaction menuState = ui.interact("dressing:menu", menuRow);
    if (state.sidebarOpen) {
        ui.fill(menuRow, SidebarOpenTop);
    } else if (menuState.hovered) {
        ui.fill(menuRow, SidebarSelected);
    }
    ui.sprite({ 9.0f, menuRow.y + 6.0f, 11.0f, 11.0f }, "ui/sidebar_icons/menu_threebars", state.sidebarOpen ? HeaderInk : White);
    if (menuState.clicked) {
        state.sidebarOpen = !state.sidebarOpen;
    }

    float y = SidebarRowTop + SidebarRowHeight;
    for (const SidebarEntry& entry : SidebarEntries) {
        Rect row { 0.0f, y, barWidth - 1.0f, SidebarRowHeight };
        Interaction rowState = ui.interact(std::string("dressing:side:") + entry.key, row);
        bool current = state.section == entry.section || (entry.section == DressingSection::Creator && (state.section == DressingSection::Category || state.section == DressingSection::Colors));
        if (current || rowState.hovered) {
            ui.fill(row, SidebarSelected);
        }
        ui.sprite({ 8.0f, row.y + 5.0f, 12.0f, 12.0f }, entry.icon);
        if (state.sidebarOpen) {
            std::string label = fitted(ui, tr(entry.key, entry.fallback), TextStyle::Pixel, barWidth - 30.0f);
            ui.text(label, TextStyle::Pixel, 23.0f, std::round(row.y + (row.h - ui.lineHeight(TextStyle::Pixel)) * 0.5f), White);
        }
        if (rowState.clicked) {
            state.trail.clear();
            openDressingSection(entry.section);
            state.trail.clear();
        }
        y += SidebarRowHeight;
    }
    ui.fill({ 0.0f, y + 2.0f, barWidth - 1.0f, 1.0f }, Divider);

    if (state.sidebarOpen) {
        Rect store { 7.0f, height - 26.0f, barWidth - 15.0f, 22.0f };
        if (ui.classicButton("dressing:store", tr("dr.go_to_store", "Go to Store"), store)) {
            navigate(Screen::Marketplace);
        }
    }
}

/**
 * My characters: the current character in the middle with the arrows to go
 * through the others, the caption, and the edit, delete and information
 * buttons, or the character alone and larger in full screen.
 */
void Menu::dressingCharacters(Context& ui, float left, float width, float height)
{
    DressingState& state = dressingState;
    float centerX = std::floor(left + width * 0.5f);
    if (state.section == DressingSection::Fullscreen) {
        playerModel(ui, centerX, HeaderHeight + 70.0f, std::min(8.5f, (height - HeaderHeight - 30.0f) / 36.0f));
        Rect toggle { left + width - 23.0f, height - 44.0f, 20.0f, 20.0f };
        ui.classicButton("dressing:unfullscreen", "", toggle);
        ui.sprite(toggle.inset(3.0f), "ui/recipe_book_expand_dots_in");
        if (ui.interact("dressing:unfullscreen", toggle).clicked) {
            state.section = DressingSection::Characters;
        }
        return;
    }
    const std::vector<util::DefaultCharacter>& characters = util::defaultCharacters();
    size_t count = characters.size() + 1;
    if (!carouselPlaced) {
        carouselIndex = 0;
        carouselPlaced = true;
    }
    std::string current = playerSkin;
    for (int offset = -2; offset <= 2; ++offset) {
        if (offset == 0) {
            continue;
        }
        size_t index = (carouselIndex + count + static_cast<size_t>(offset + 2) - 2) % count;
        playerSkin = index == 0 ? current : characterSprite(ui, characters[index - 1].file);
        float spacing = std::abs(offset) == 1 ? 118.0f : 200.0f;
        playerModel(ui, centerX + static_cast<float>(offset) * spacing, HeaderHeight + 50.0f, std::abs(offset) == 1 ? 1.9f : 1.4f);
    }
    playerSkin = carouselIndex == 0 ? current : characterSprite(ui, characters[carouselIndex - 1].file);
    playerModel(ui, centerX, HeaderHeight + 26.0f, 2.95f);
    playerSkin = current;
    if (ui.classicButton("dressing:previous", "<", { centerX - 59.0f, HeaderHeight + 51.0f, 22.0f, 22.0f })) {
        carouselIndex = (carouselIndex + count - 1) % count;
    }
    if (ui.classicButton("dressing:next", ">", { centerX + 37.0f, HeaderHeight + 51.0f, 22.0f, 22.0f })) {
        carouselIndex = (carouselIndex + 1) % count;
    }

    if (carouselIndex != 0) {
        const util::DefaultCharacter& character = characters[carouselIndex - 1];
        float captionY = HeaderHeight + 132.0f;
        ui.textCentered(character.name, TextStyle::Body, { left, captionY, width, 10.0f }, White);
        Rect equip { centerX - 65.0f, captionY + 13.0f, 130.0f, 20.0f };
        if (ui.classicButton("dressing:use_character", tr("dr.button.equip", "Equip"), equip)) {
            equipCharacter(ui, carouselIndex - 1);
        }
        return;
    }

    std::string caption = tr("dr.label.current_persona", "This is your current character");
    float captionWidth = ui.measure(caption, TextStyle::Body);
    float captionX = std::floor(centerX - (captionWidth + 14.0f) * 0.5f);
    float captionY = HeaderHeight + 132.0f;
    ui.sprite({ captionX, captionY + 1.0f, 10.0f, 10.0f }, "ui/glyph_persona_small");
    ui.text(caption, TextStyle::Body, captionX + 14.0f, captionY, White);

    float buttonsY = captionY + 13.0f;
    Rect edit { centerX - 95.0f, buttonsY, 130.0f, 20.0f };
    if (ui.classicButton("dressing:edit", tr("profileScreen.manage_button_text", "Edit Character"), edit)) {
        openDressingSection(DressingSection::Creator);
    }
    Rect trash { edit.x + edit.w + 2.0f, buttonsY, 26.0f, 20.0f };
    if (ui.classicButton("dressing:delete", "", trash)) {
        state.dialog = DressingDialog::DeleteCharacter;
    }
    ui.sprite({ trash.x + 6.0f, trash.y + 3.0f, 14.0f, 14.0f }, "ui/trash_default");
    Rect info { trash.x + trash.w + 5.0f, buttonsY, 26.0f, 20.0f };
    if (ui.classicButton("dressing:differences", "", info)) {
        state.dialog = DressingDialog::Differences;
    }
    ui.sprite({ info.x + 6.0f, info.y + 3.0f, 14.0f, 14.0f }, "ui/infobulb");

    Rect toggle { left + width - 23.0f, height - 44.0f, 20.0f, 20.0f };
    if (ui.classicButton("dressing:fullscreen", "", toggle)) {
        state.section = DressingSection::Fullscreen;
    }
    ui.sprite(toggle.inset(3.0f), "ui/recipe_book_expand_dots_out");
}

/**
 * The character creator's category list: the body and style groups, each
 * opening and closing like the game's accordion, every row leading to the
 * pieces of its category.
 */
void Menu::dressingCreator(Context& ui, const Rect& panel)
{
    DressingState& state = dressingState;
    float x = panel.x + 6.0f;
    float w = panel.w - 16.0f;
    float y = panel.y + 6.0f;
    auto group = [&](const char* id, const std::string& label, bool& open, const auto& categories) {
        Rect head { x, y, w, 26.0f };
        if (ui.classicButton(id, "", head)) {
            open = !open;
        }
        ui.text(label, TextStyle::Pixel, head.x + 4.0f, std::round(head.y + (head.h - ui.lineHeight(TextStyle::Pixel)) * 0.5f), ButtonText);
        ui.sprite({ head.x + head.w - 16.0f, head.y + 10.0f, 10.0f, 6.0f }, open ? "hbui/arrowUp" : "hbui/arrowDown", ButtonText);
        y += head.h + 1.0f;
        if (!open) {
            return;
        }
        for (const Category& category : categories) {
            Rect row { x, y, w, RowHeight };
            Interaction rowState = ui.interact(std::string("dressing:category:") + category.page, row);
            if (rowState.hovered) {
                ui.fill(row, { 255, 255, 255, 18 });
            }
            std::string name = tr(category.key, category.fallback);
            ui.text(name, TextStyle::Pixel, row.x + 2.0f, std::round(row.y + (row.h - ui.lineHeight(TextStyle::Pixel)) * 0.5f), White);
            Rect icon { row.x + row.w - 16.0f, row.y + 5.0f, 15.0f, 15.0f };
            panelFrame(ui, icon, { 30, 30, 31, 255 }, { 110, 110, 111, 255 });
            ui.sprite(icon.inset(2.0f), category.icon);
            ui.fill({ row.x, row.y + row.h - 1.0f, row.w, 1.0f }, RowLine);
            if (rowState.clicked) {
                openDressingSection(DressingSection::Category, category.page, name);
            }
            y += RowHeight;
        }
    };
    group("dressing:body", tr("dr.categories.body", "Body"), state.bodyOpen, BodyCategories);
    group("dressing:style", tr("dr.categories.style", "Style"), state.styleOpen, StyleCategories);
}

/**
 * A grid of pieces the way the store lays out a category: the owned ones
 * under their heading with the button that takes the piece off, then the
 * others, every tile tinted by rarity with its thumbnail, rarity strip and
 * coin or lock badge, scrolled with the wheel.
 */
void Menu::dressingPieces(Context& ui, const Rect& panel, const std::string& pageId, std::string_view ownedTitle, std::string_view othersTitle, std::string_view noneLabel)
{
    DressingState& state = dressingState;
    Rect inner { panel.x + 5.0f, panel.y + 4.0f, panel.w - 16.0f, panel.h - 8.0f };
    const DressingPageView* page = nullptr;
    if (auto found = dressing.find(pageId); found != dressing.end()) {
        page = &found->second;
    }
    float tile = std::min(TileSize, std::floor((inner.w - (TileColumns - 1) * (TileStep - TileSize)) / TileColumns));
    float step = tile + (TileStep - TileSize);

    auto rows = [&](size_t count) {
        return static_cast<float>((count + TileColumns - 1) / TileColumns);
    };
    float content = 40.0f + (page ? rows(page->owned.size()) * step + (page->others.empty() ? 0.0f : 28.0f + rows(page->others.size()) * step) : 0.0f);
    float range = std::max(0.0f, content - inner.h);
    if (inner.contains(ui.mouseX(), ui.mouseY())) {
        state.scroll -= ui.input().wheel * step * 0.5f;
    }
    state.scroll = std::clamp(state.scroll, 0.0f, range);

    ui.setClip(inner);
    float y = inner.y - state.scroll;
    ui.text(upper(std::string(ownedTitle)), TextStyle::HeadingSmall, inner.x + 1.0f, y + 3.0f, White);
    y += 15.0f;
    if (ui.classicButton("dressing:none", noneLabel, { inner.x, y, inner.w, 18.0f })) {
        state.equipped.clear();
        state.selected.clear();
    }
    y += 22.0f;

    auto matches = [&](const DressingPiece& piece) {
        if (state.search.empty()) {
            return true;
        }
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        return lower(piece.title).find(lower(state.search)) != std::string::npos;
    };
    auto grid = [&](const std::vector<DressingPiece>& all) {
        std::vector<DressingPiece> pieces;
        std::copy_if(all.begin(), all.end(), std::back_inserter(pieces), matches);
        for (size_t index = 0; index < pieces.size(); ++index) {
            const DressingPiece& piece = pieces[index];
            Rect cell { inner.x + static_cast<float>(index % TileColumns) * step, y + static_cast<float>(index / TileColumns) * step, tile, tile };
            if (cell.y + cell.h < inner.y || cell.y > inner.y + inner.h) {
                continue;
            }
            Interaction cellState = ui.interact("dressing:piece:" + piece.id, cell);
            ui.fill(cell, rarityColor(piece.rarity));
            if (!piece.sprite.empty()) {
                ui.sprite(cell, piece.sprite);
            }
            if (cellState.hovered) {
                ui.fill(cell, { 255, 255, 255, 30 });
            }
            ui.sprite({ cell.x, cell.y + cell.h - 4.0f, 22.0f, 4.0f }, "ui/rarity_" + (piece.rarity.empty() ? std::string("common") : piece.rarity));
            if (!piece.owned) {
                ui.sprite({ cell.x + cell.w - 9.0f, cell.y + cell.h - 9.0f, 8.0f, 8.0f }, "ui/icon_minecoin_9x9");
            }
            if (piece.id == state.selected || piece.id == state.equipped) {
                ui.nineSlice(cell, "ui/equipped_item_border");
            }
            if (cellState.clicked) {
                state.selected = piece.id;
            }
        }
        y += rows(pieces.size()) * step;
    };

    if (!pageId.empty() && (!page || page->loading || !page->loaded)) {
        ui.textCentered(tr("dr.loading", "Loading..."), TextStyle::Body, { inner.x, y + 10.0f, inner.w, 12.0f }, Muted1);
    } else if (page && !page->error.empty()) {
        ui.textCentered(page->error, TextStyle::Body, { inner.x, y + 10.0f, inner.w, 12.0f }, Muted1);
    } else if (page) {
        grid(page->owned);
        if (!page->others.empty() && !othersTitle.empty()) {
            ui.fill({ inner.x + 4.0f, y + 6.0f, inner.w - 8.0f, 2.0f }, RowLine);
            ui.text(upper(std::string(othersTitle)), TextStyle::HeadingSmall, inner.x + 1.0f, y + 14.0f, White);
            y += 28.0f;
            grid(page->others);
        } else if (!page->others.empty()) {
            grid(page->others);
        }
    }
    ui.clearClip();

    if (range > 0.0f) {
        Rect track { panel.x + panel.w - 8.0f, panel.y + 6.0f, 3.0f, panel.h - 12.0f };
        ui.fill(track, { 20, 20, 21, 255 });
        float thumb = std::max(12.0f, track.h * inner.h / content);
        ui.fill({ track.x, track.y + (track.h - thumb) * state.scroll / range, track.w, thumb }, Muted1);
    }
}

/**
 * The color picker of a colorable category: its title over the grid of
 * swatches, the chosen one framed.
 */
void Menu::dressingColors(Context& ui, const Rect& panel)
{
    DressingState& state = dressingState;
    Rect inner { panel.x + 5.0f, panel.y + 4.0f, panel.w - 16.0f, panel.h - 8.0f };
    ui.textCentered(tr("dr.hair_color", "Hair"), TextStyle::Body, { inner.x, inner.y + 5.0f, inner.w, 12.0f }, White);
    float tile = std::min(TileSize, std::floor((inner.w - (TileColumns - 1) * (TileStep - TileSize)) / TileColumns));
    float step = tile + (TileStep - TileSize);
    for (size_t index = 0; index < HairColors.size(); ++index) {
        Rect cell { inner.x + static_cast<float>(index % TileColumns) * step, inner.y + 22.0f + static_cast<float>(index / TileColumns) * step, tile, tile };
        Interaction cellState = ui.interact("dressing:color:" + std::to_string(index), cell);
        ui.fill(cell, HairColors[index]);
        if (cellState.hovered || state.color == static_cast<int>(index)) {
            ui.nineSlice(cell, "ui/equipped_item_border");
        }
        if (cellState.clicked) {
            state.color = static_cast<int>(index);
        }
    }
}

/**
 * The right side of a list page: the character over the room, the color and
 * full screen buttons, and under it the getting started note or the chosen
 * piece with its rarity, creator and the equip button.
 */
void Menu::dressingPreview(Context& ui, const Rect& area, bool colorable)
{
    DressingState& state = dressingState;
    float infoTop = std::floor(area.y + area.h * 0.72f);
    playerModel(ui, area.x + area.w * 0.5f, area.y + 30.0f, std::min(6.0f, (infoTop - area.y - 40.0f) / 34.0f));

    Rect toggle { area.x + area.w - 23.0f, infoTop - 40.0f, 20.0f, 20.0f };
    if (ui.classicButton("dressing:preview_fullscreen", "", toggle)) {
        state.trail.push_back(state.section);
        state.section = DressingSection::Fullscreen;
    }
    ui.sprite(toggle.inset(3.0f), "ui/recipe_book_expand_dots_out");
    if (colorable && state.section == DressingSection::Category) {
        Rect paint { area.x + 2.0f, infoTop - 40.0f, 20.0f, 20.0f };
        if (ui.classicButton("dressing:color", "", paint)) {
            openDressingSection(DressingSection::Colors);
        }
        ui.sprite(paint.inset(3.0f), "ui/text_color_paintbrush");
    }

    Rect info { area.x, infoTop, area.w, area.y + area.h - infoTop };
    ui.fill(info, InfoFill);
    Rect bar { info.x, info.y, info.w, 14.0f };
    ui.fill(bar, InfoTitleFill);

    const DressingPiece* chosen = nullptr;
    for (const auto& [id, page] : dressing) {
        for (const auto* list : { &page.owned, &page.others }) {
            for (const DressingPiece& piece : *list) {
                if (piece.id == state.selected) {
                    chosen = &piece;
                }
            }
        }
    }
    if (state.section == DressingSection::ClassicSkins) {
        if (classicSelected.empty()) {
            ui.text(upper(tr("dr.character_creator_getting_started_title", "Getting Started")), TextStyle::HeadingSmall, bar.x + 4.0f, bar.y + 3.0f, White);
            ui.paragraph(tr("dr.classic_skins.custom_skin_description", "Import a .png image (64x32, 64x64 or 128x128) from your device."), TextStyle::Body, info.x + 5.0f, bar.y + bar.h + 5.0f, info.w - 10.0f, White);
            return;
        }
        std::string name = classicSelected == "custom" ? tr("dr.classic_skins.right_side.custom_skin", "Custom Skin") : tr("dr.default." + classicSelected + ".skin", classicSelected);
        ui.text(upper(name), TextStyle::HeadingSmall, bar.x + 4.0f, bar.y + 3.0f, White);
        if (classicSelected != "custom") {
            ui.text(tr("dr.classic_skins.right_side.author_minecraft", "by Minecraft"), TextStyle::Body, info.x + 5.0f, bar.y + bar.h + 4.0f, White);
        }
        Rect equip { info.x + 3.0f, info.y + info.h - 19.0f, std::min(143.0f, info.w - 6.0f), 17.0f };
        if (ui.classicButton("dressing:classic_equip", tr("dr.equip_piece", "Equip"), equip)) {
            util::SkinChoice choice;
            choice.kind = classicSelected;
            choice.slim = classicSelected == "alex" || (classicSelected == "custom" && importedSlim);
            util::saveSkinChoice(choice);
            applySkinChoice(ui);
            notify(trf("dr.notification.equipped_classic_skin", "You equipped %s", { name }));
        }
        return;
    }
    if (state.section == DressingSection::Colors) {
        chosen = nullptr;
    } else if (!chosen) {
        ui.text(upper(tr("dr.character_creator_getting_started_title", "Getting Started")), TextStyle::HeadingSmall, bar.x + 4.0f, bar.y + 3.0f, White);
        ui.paragraph(tr("dr.character_creator_getting_started_detail", "Select items on the left to see how they look on your character!"), TextStyle::Body, info.x + 5.0f, bar.y + bar.h + 5.0f, info.w - 10.0f, White);
        return;
    }
    if (chosen) {
        ui.text(upper(chosen->title), TextStyle::HeadingSmall, bar.x + 4.0f, bar.y + 3.0f, White);
        std::string rarity = tr("dr.rarity." + (chosen->rarity.empty() ? std::string("common") : chosen->rarity), chosen->rarity);
        ui.text(rarity, TextStyle::Pixel, bar.x + bar.w - ui.measure(rarity, TextStyle::Pixel) - 4.0f, bar.y + 3.0f, White);
        std::string creator = trf("store.createdBy", "By %s", { chosen->creator });
        ui.text(creator, TextStyle::Body, info.x + 5.0f, bar.y + bar.h + 4.0f, White);
        ui.fill({ info.x + 5.0f, bar.y + bar.h + 15.0f, ui.measure(creator, TextStyle::Body), 1.0f }, White);
    }
    Rect equip { info.x + 3.0f, info.y + info.h - 19.0f, std::min(143.0f, info.w - 6.0f), 17.0f };
    if (ui.classicButton("dressing:equip", tr("dr.equip_piece", "Equip"), equip, state.section == DressingSection::Colors || (chosen && chosen->owned))) {
        if (chosen) {
            state.equipped = chosen->id;
        }
    }
}

/**
 * Puts the saved classic skin on the character the menus draw: Steve, Alex
 * or the imported image.
 */
void Menu::applySkinChoice(Context& ui)
{
    util::SkinChoice choice = util::loadSkinChoice();
    importedSlim = choice.kind == "custom" && choice.slim;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
    if (choice.kind == "custom" && util::readCustomSkin(width, height, rgba)) {
        ui.skin().setDynamic("dynamic/custom_skin", { width, height, std::move(rgba) });
        playerSkin = "dynamic/custom_skin";
    } else if (choice.kind.rfind("default:", 0) == 0) {
        playerSkin = characterSprite(ui, choice.kind.substr(8));
    } else {
        playerSkin = choice.kind == "alex" ? "textures/entity/alex" : "textures/entity/steve";
    }
}

/**
 * The sprite holding a default character's skin, read from the game's
 * vanilla skin pack the first time it is drawn.
 */
std::string Menu::characterSprite(Context& ui, const std::string& file)
{
    std::string name = "dynamic/default_skin_" + file;
    if (!ui.skin().sprite(name).valid) {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
        if (!util::readDefaultSkin(file, width, height, rgba)) {
            return "textures/entity/steve";
        }
        ui.skin().setDynamic(name, { width, height, std::move(rgba) });
    }
    return name;
}

/**
 * Wears a default character: saves it as the skin sent on the next join and
 * puts it on the character the menus draw.
 */
void Menu::equipCharacter(Context& ui, size_t index)
{
    const util::DefaultCharacter& character = util::defaultCharacters()[index];
    util::SkinChoice choice;
    choice.kind = "default:" + character.file;
    choice.slim = character.slim;
    util::saveSkinChoice(choice);
    applySkinChoice(ui);
    carouselIndex = 0;
}

/**
 * The classic skins the player owns: the tile to import their own image,
 * then Steve, Alex and the imported skin, each shown on the character.
 */
void Menu::dressingClassicSkins(Context& ui, const Rect& panel)
{
    Rect inner { panel.x + 5.0f, panel.y + 4.0f, panel.w - 16.0f, panel.h - 8.0f };
    ui.text(upper(tr("dr.classic_skins.custom_skin_section_title", "Owned Skins")), TextStyle::HeadingSmall, inner.x + 1.0f, inner.y + 3.0f, White);
    float tile = std::min(TileSize, std::floor((inner.w - (TileColumns - 1) * (TileStep - TileSize)) / TileColumns));
    float step = tile + (TileStep - TileSize);
    std::vector<std::string> skins { "import", "steve", "alex" };
    std::error_code error;
    if (std::filesystem::exists(util::customSkinPath(), error)) {
        skins.push_back("custom");
    }
    for (size_t index = 0; index < skins.size(); ++index) {
        const std::string& skin = skins[index];
        Rect cell { inner.x + static_cast<float>(index % TileColumns) * step, inner.y + 16.0f + static_cast<float>(index / TileColumns) * step, tile, tile };
        Interaction cellState = ui.interact("dressing:classic:" + skin, cell);
        ui.fill(cell, { 108, 108, 108, 255 });
        if (skin == "import") {
            ui.textCentered("+", TextStyle::HeadingLarge, { cell.x, cell.y + 8.0f, cell.w, 20.0f }, White);
            ui.paragraph(tr("dr.classic_skins.choose_custom_skin", "Choose New Skin"), TextStyle::BodySmall, cell.x + 3.0f, cell.y + 32.0f, cell.w - 6.0f, White);
        } else {
            std::string previous = playerSkin;
            if (skin == "custom") {
                uint32_t width = 0;
                uint32_t height = 0;
                std::vector<uint8_t> rgba;
                if (!ui.skin().sprite("dynamic/custom_skin").valid && util::readCustomSkin(width, height, rgba)) {
                    ui.skin().setDynamic("dynamic/custom_skin", { width, height, std::move(rgba) });
                }
                playerSkin = "dynamic/custom_skin";
            } else {
                playerSkin = "textures/entity/" + skin;
            }
            playerModel(ui, cell.x + cell.w * 0.5f, cell.y + 5.0f, (cell.h - 8.0f) / 34.0f);
            playerSkin = previous;
        }
        if (cellState.hovered) {
            ui.fill(cell, { 255, 255, 255, 30 });
        }
        if (skin == classicSelected) {
            ui.nineSlice(cell, "ui/equipped_item_border");
        }
        if (cellState.clicked) {
            if (skin == "import") {
                std::string source = platform::pickPngFile();
                std::string failure;
                if (!source.empty()) {
                    if (util::importCustomSkin(source, failure)) {
                        ui.skin().clearDynamic("dynamic/custom_skin");
                        classicSelected = "custom";
                        dressingState.dialog = DressingDialog::SkinModel;
                    } else {
                        notify(tr("dr.classic_skins.invalidCustomSkin", "Please import a 64x64, 64x32 or 128x128 .png file"));
                    }
                }
            } else {
                classicSelected = skin;
            }
        }
    }
}

/**
 * The Minecoin offers the store lists for the dressing room: a row of coin
 * packs with their art, coins and bonus over the price bar, and the starter
 * bundles under them. Prices come from the platform store, which Kestrel
 * cannot reach, so the bars stay empty.
 */
void Menu::dressingCoins(Context& ui, float left, float width, float height)
{
    constexpr Color PriceBar { 232, 200, 40, 255 };
    constexpr Color CoinText { 255, 222, 60, 255 };
    auto found = dressing.find(CoinPage);
    Rect area { left + 7.0f, HeaderHeight + 8.0f, width - 12.0f, height - HeaderHeight - 12.0f };
    if (found == dressing.end() || found->second.loading || !found->second.loaded) {
        ui.textCentered(tr("dr.loading", "Loading..."), TextStyle::Body, { area.x, area.y + 40.0f, area.w, 12.0f }, Muted1);
        return;
    }
    std::vector<const DressingPiece*> packs;
    std::vector<const DressingPiece*> bundles;
    for (const auto* list : { &found->second.owned, &found->second.others }) {
        for (const DressingPiece& piece : *list) {
            (piece.packType == "CoinBundle" ? bundles : packs).push_back(&piece);
        }
    }
    auto coinLine = [&](int amount, float centerX, float y, Color color) {
        std::string value = std::to_string(amount);
        float valueWidth = ui.measure(value, TextStyle::Pixel);
        float x = std::floor(centerX - (valueWidth + 11.0f) * 0.5f);
        ui.sprite({ x, y, 9.0f, 9.0f }, "ui/icon_minecoin_9x9");
        ui.text(value, TextStyle::Pixel, x + 11.0f, y + 1.0f, color);
    };

    float gap = 3.0f;
    float tileWidth = packs.empty() ? 0.0f : std::floor((area.w - gap * static_cast<float>(packs.size() - 1)) / static_cast<float>(packs.size()));
    float tileHeight = std::floor(std::min(206.0f, area.h * 0.55f));
    for (size_t index = 0; index < packs.size(); ++index) {
        const DressingPiece& pack = *packs[index];
        Rect tile { area.x + static_cast<float>(index) * (tileWidth + gap), area.y + 10.0f, tileWidth, tileHeight - 10.0f };
        Interaction state = ui.interact("dressing:coinpack:" + pack.id, tile);
        panelFrame(ui, tile, { 30, 30, 31, 255 }, state.hovered ? White : Color { 60, 60, 61, 255 });
        float art = std::min(tile.w - 20.0f, 72.0f);
        if (!pack.sprite.empty()) {
            ui.sprite({ std::floor(tile.x + (tile.w - art) * 0.5f), tile.y + 26.0f, art, art }, pack.sprite);
        }
        float centerX = tile.x + tile.w * 0.5f;
        float lines = tile.y + tile.h - 60.0f;
        if (pack.bonus > 0) {
            coinLine(pack.coins - pack.bonus, centerX, lines, CoinText);
            ui.textCentered("+", TextStyle::Pixel, { tile.x, lines + 11.0f, tile.w, 10.0f }, White);
            ui.textCentered(tr("store.coin.bonus", "Bonus!"), TextStyle::Pixel, { tile.x, lines + 22.0f, tile.w, 10.0f }, White);
            coinLine(pack.bonus, centerX, lines + 33.0f, CoinText);
        } else {
            coinLine(pack.coins, centerX, lines + 12.0f, CoinText);
        }
        ui.fill({ tile.x, tile.y + tile.h - 13.0f, tile.w, 13.0f }, PriceBar);
    }

    float bundleTop = area.y + tileHeight + 6.0f;
    float bundleWidth = bundles.empty() ? 0.0f : std::floor((area.w - gap * static_cast<float>(bundles.size() - 1)) / static_cast<float>(bundles.size()));
    for (size_t index = 0; index < bundles.size(); ++index) {
        const DressingPiece& bundle = *bundles[index];
        Rect tile { area.x + static_cast<float>(index) * (bundleWidth + gap), bundleTop, bundleWidth, 66.0f };
        Interaction state = ui.interact("dressing:bundle:" + bundle.id, tile);
        panelFrame(ui, tile, { 12, 12, 12, 255 }, state.hovered ? White : Color { 60, 60, 61, 255 });
        ui.text(bundle.title, TextStyle::Body, tile.x + 5.0f, tile.y + 4.0f, White);
        ui.text(bundle.header, TextStyle::Pixel, tile.x + 5.0f, tile.y + 16.0f, White);
        std::string amount = bundle.coinText.substr(0, bundle.coinText.find(' '));
        ui.text(amount, TextStyle::Pixel, tile.x + 5.0f, tile.y + 26.0f, White);
        ui.sprite({ tile.x + 8.0f + ui.measure(amount, TextStyle::Pixel), tile.y + 25.0f, 9.0f, 9.0f }, "ui/icon_minecoin_9x9");
        ui.textCentered(bundle.footer, TextStyle::Pixel, { tile.x, tile.y + 38.0f, tile.w * 0.75f, 10.0f }, White);
        if (!bundle.sprite.empty()) {
            ui.sprite({ tile.x + tile.w - 72.0f, tile.y + 4.0f, 66.0f, 44.0f }, bundle.sprite);
        }
        ui.fill({ tile.x, tile.y + tile.h - 13.0f, tile.w, 13.0f }, PriceBar);
    }
}

/**
 * The dressing room's dialogs: confirming the character's deletion, and the
 * differences between characters and classic skins.
 */
void Menu::dressingDialog(Context& ui, float width, float height)
{
    DressingState& state = dressingState;
    if (state.dialog == DressingDialog::None) {
        return;
    }
    ui.fill({ 0.0f, 0.0f, width, height }, { 0, 0, 0, 110 });
    if (state.dialog == DressingDialog::SkinModel) {
        Rect box { std::floor((width - 220.0f) * 0.5f), std::floor(height * 0.3f), 220.0f, 118.0f };
        ui.nineSlice(box, "ui/dialog_background_opaque");
        ui.textCentered(tr("dr.classic_skins.select_skin.title", "Skin Type"), TextStyle::Pixel, { box.x, box.y + 7.0f, box.w, 10.0f }, ButtonText);
        Rect well { box.x + 6.0f, box.y + 20.0f, box.w - 12.0f, box.h - 26.0f };
        ui.fill(well, { 30, 30, 31, 255 });
        ui.paragraph(tr("dr.classic_skins.select_skin", "Choose the model that matches your skin."), TextStyle::Body, well.x + 6.0f, well.y + 6.0f, well.w - 12.0f, White);
        float half = std::floor((well.w - 12.0f) * 0.5f);
        if (ui.classicButton("dressing:skin_wide", tr("persona.wide.title", "wide"), { well.x + 4.0f, well.y + well.h - 30.0f, half, 26.0f })) {
            importedSlim = false;
            state.dialog = DressingDialog::None;
        }
        if (ui.classicButton("dressing:skin_slim", tr("persona.slim.title", "slim"), { well.x + 8.0f + half, well.y + well.h - 30.0f, half, 26.0f })) {
            importedSlim = true;
            state.dialog = DressingDialog::None;
        }
        return;
    }
    if (state.dialog == DressingDialog::DeleteCharacter) {
        Rect box { std::floor((width - 256.0f) * 0.5f), std::floor(height * 0.34f), 256.0f, 127.0f };
        ui.nineSlice(box, "ui/dialog_background_opaque");
        ui.textCentered(tr("dr.modal.persona_delete_confirm_title", "Delete Character?"), TextStyle::Pixel, { box.x, box.y + 7.0f, box.w, 10.0f }, ButtonText);
        Rect well { box.x + 6.0f, box.y + 20.0f, box.w - 12.0f, box.h - 26.0f };
        ui.fill(well, { 30, 30, 31, 255 });
        ui.sprite({ well.x + 4.0f, well.y + 3.0f, 44.0f, 66.0f }, "ui/alex_icon_arms_up");
        ui.paragraph(tr("dr.modal.persona_delete_confirm", "The current character will be deleted. Are you sure you want to continue?"), TextStyle::Body, well.x + 56.0f, well.y + 6.0f, well.w - 60.0f, White);
        float half = std::floor((well.w - 12.0f) * 0.5f);
        if (ui.classicButton("dressing:delete_cancel", tr("gui.cancel", "Cancel"), { well.x + 4.0f, well.y + well.h - 30.0f, half, 26.0f })) {
            state.dialog = DressingDialog::None;
        }
        if (ui.classicButton("dressing:delete_confirm", tr("gui.confirm", "Confirm"), { well.x + 8.0f + half, well.y + well.h - 30.0f, half, 26.0f })) {
            state.dialog = DressingDialog::None;
            notify(tr("dr.notification.persona_delete", "Character deleted"));
        }
        return;
    }
    Rect box { std::floor(width * 0.1f), std::floor(height * 0.03f), std::floor(width * 0.8f), std::floor(height * 0.94f) };
    ui.nineSlice(box, "ui/dialog_background_opaque");
    Rect well { box.x + 7.0f, box.y + 20.0f, box.w - 14.0f, box.h - 64.0f };
    ui.fill(well, { 30, 30, 31, 255 });
    float imageWidth = 116.0f;
    auto section = [&](float top, const char* icon, const std::string& heading, const std::string& body, const char* image) {
        ui.sprite({ well.x + 9.0f, top + 5.0f, 11.0f, 11.0f }, icon);
        ui.text(upper(heading), TextStyle::HeadingSmall, well.x + 25.0f, top + 7.0f, White);
        ui.paragraph(body, TextStyle::Body, well.x + 9.0f, top + 22.0f, well.w - imageWidth - 30.0f, White);
        ui.sprite({ well.x + well.w - imageWidth - 12.0f, top + 4.0f, imageWidth, 68.0f }, image);
    };
    section(well.y, "ui/sidebar_icons/character_creator", tr("profileScreen.differences_character_creator_title", "Character"), tr("profileScreen.difference_character_creator_description", ""), "ui/dressing_room_customization");
    ui.fill({ well.x + 9.0f, well.y + 82.0f, well.w - 30.0f, 1.0f }, { 90, 110, 110, 255 });
    section(well.y + 89.0f, "ui/sidebar_icons/classic_skins", tr("profileScreen.differences_classic_skin_title", "Classic Skin"), tr("profileScreen.difference_classic_skin_description", ""), "ui/dressing_room_skins");
    if (ui.classicButton("dressing:differences_ok", tr("gui.ok", "OK"), { box.x + 11.0f, box.y + box.h - 40.0f, box.w - 22.0f, 26.0f })) {
        state.dialog = DressingDialog::None;
    }
}

}
