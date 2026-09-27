#include "client/Client.h"

#include "client/DebugLog.h"
#include "ui/GameAssets.h"
#include "util/JsonText.h"

namespace kestrel {

namespace {

// Keeps HD glyph sheets and textures from filling the UI atlas; glyphs keep their size, see Font.
constexpr uint32_t MaxGlyphSheet = 512;
constexpr uint32_t MaxPackTexture = 1024;

// The HUD files Kestrel draws from, besides the ones a pack lists in its _ui_defs.json.
constexpr const char* HudFiles[] = { "ui/_global_variables.json", "ui/scoreboards.json" };
constexpr const char* TextureExtensions[] = { ".png", ".jpg", ".jpeg", ".tga" };

// Not in the game's files, the game fills these bindings from code.
constexpr double SidebarBackgroundOpacity = 0.3;
constexpr double SidebarTitleBackgroundOpacity = 0.3;

}

/**
 * Puts the server's packs over the game's: their glyph sheets, their HUD
 * definitions and the textures those use. Leaving the server brings the
 * vanilla look back.
 */
void Client::applyServerPacks(const std::vector<std::shared_ptr<const world::PackFiles>>& packs)
{
    if (hudUiLoaded && packs == artPacks) {
        return;
    }
    artPacks = packs;
    hudUiLoaded = true;
    for (const std::string& name : packSprites) {
        skin.clearDynamic(name);
    }
    packSprites.clear();
    loadPackGlyphs(packs);
    loadHudUi(packs);
}

/**
 * Every glyph sheet a pack ships replaces the vanilla one, the first pack in
 * the stack winning.
 */
void Client::loadPackGlyphs(const std::vector<std::shared_ptr<const world::PackFiles>>& packs)
{
    for (size_t index = 0; index < ui::Font::PixelPageCount; ++index) {
        font.setPixelPageSourceWidth(index, 0);
        std::string name = ui::Font::pixelPageName(index);
        for (const std::shared_ptr<const world::PackFiles>& pack : packs) {
            const std::string* encoded = pack->find(name + ".png");
            if (!encoded) {
                continue;
            }
            ui::Bitmap sheet;
            if (ui::decodeBitmap(*encoded, sheet)) {
                font.setPixelPageSourceWidth(index, sheet.width);
                skin.setDynamic(name, ui::shrinkBitmap(sheet, MaxGlyphSheet));
                packSprites.push_back(name);
                debugLog("pack glyph sheet " + name + " " + std::to_string(sheet.width) + "px");
            }
            break;
        }
    }
    font.reloadPixelPages();
}

/**
 * Merges the vanilla HUD files with every pack's copy, lowest priority pack
 * first, plus the files packs add through _ui_defs.json, then loads the pack
 * textures the merged controls name.
 */
void Client::loadHudUi(const std::vector<std::shared_ptr<const world::PackFiles>>& packs)
{
    auto definitions = std::make_shared<ui::JsonUi>();
    for (const char* path : HudFiles) {
        std::string text;
        std::string file(path);
        if (assets.readArchived("ui", file.substr(3), text)) {
            definitions->addFile(file, text);
        } else if (std::vector<unsigned char> loose = assets.readPackFile(file); !loose.empty()) {
            definitions->addFile(file, std::string(loose.begin(), loose.end()));
        }
    }
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        std::vector<std::string> paths(std::begin(HudFiles), std::end(HudFiles));
        if (const std::string* defs = (*pack)->find("ui/_ui_defs.json")) {
            if (std::unique_ptr<json::Value> root = util::parseJsonObject(*defs)) {
                if (const json::Value* list = root->get("ui_defs"); list && list->isArray()) {
                    for (const std::unique_ptr<json::Value>& entry : list->mArray) {
                        paths.push_back(entry->string());
                    }
                }
            }
        }
        for (const std::string& path : paths) {
            if (const std::string* text = (*pack)->find(path)) {
                definitions->addFile(path, *text);
            }
        }
    }

    for (const std::string& texture : definitions->texturePaths()) {
        for (const std::shared_ptr<const world::PackFiles>& pack : packs) {
            const std::string* encoded = nullptr;
            for (const char* extension : TextureExtensions) {
                if ((encoded = pack->find(texture + extension))) {
                    break;
                }
            }
            if (!encoded) {
                continue;
            }
            ui::Bitmap bitmap;
            if (ui::decodeBitmap(*encoded, bitmap)) {
                ui::NineSlice slice;
                if (const std::string* sliceJson = pack->find(texture + ".json")) {
                    ui::readNineSlice(*sliceJson, slice);
                }
                float fit = bitmap.width > MaxPackTexture ? static_cast<float>(MaxPackTexture) / static_cast<float>(bitmap.width) : 1.0f;
                slice = { slice.left * fit, slice.top * fit, slice.right * fit, slice.bottom * fit };
                skin.setDynamic(texture, ui::shrinkBitmap(bitmap, MaxPackTexture), slice);
                packSprites.push_back(texture);
                debugLog("pack ui texture " + texture);
            }
            break;
        }
    }
    menu.setHudUi(std::move(definitions));
}

/**
 * The sidebar objective as the scoreboard controls bind it.
 */
ui::UiData Client::sidebarData() const
{
    ui::UiData data;
    data.globals["#objective_sidebar_name"] = ui::UiValue::of(sidebarView.title);
    data.globals["#scoreboard_sidebar_visible"] = ui::UiValue::of(sidebarView.visible);
    data.globals["#scoreboard_sidebar_size"] = ui::UiValue::of(static_cast<double>(sidebarView.lines.size()));
    data.globals["#objective_background_opacity"] = ui::UiValue::of(SidebarBackgroundOpacity);
    data.globals["#scoreboard_objective_background_opacity"] = ui::UiValue::of(SidebarTitleBackgroundOpacity);
    std::vector<ui::UiRow>& players = data.collections["scoreboard_players"];
    std::vector<ui::UiRow>& scores = data.collections["scoreboard_scores"];
    for (const auto& [name, score] : sidebarView.lines) {
        players.push_back({ { "#player_name_sidebar", ui::UiValue::of(name) } });
        scores.push_back({ { "#player_score_sidebar", ui::UiValue::of(std::to_string(score)) } });
    }
    return data;
}

}
