#include "client/Client.h"

#include "client/DebugLog.h"
#include "ui/GameAssets.h"
#include "util/JsonText.h"

#include <algorithm>

namespace kestrel {

namespace {

// Keeps huge glyphs and textures from filling the UI atlas; glyphs keep their drawn size, see Font.
constexpr uint32_t MaxGlyphSprite = 256;
constexpr uint32_t MaxPackTexture = 1024;

// Read before the files _ui_defs.json lists, which it leaves out.
constexpr const char* GlobalVariablesFile = "ui/_global_variables.json";
constexpr const char* UiDefsFile = "ui/_ui_defs.json";
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
    if (jsonUiLoaded && packs == artPacks) {
        return;
    }
    StartupTimer timer;
    artPacks = packs;
    assets.setResourcePacks(packs);
    skin.reloadPackImages();
    jsonUiLoaded = true;
    for (const std::string& name : packSprites) {
        skin.clearDynamic(name);
    }
    packSprites.clear();
    timer.mark("packs: resource packs and images");
    loadPackGlyphs(packs);
    timer.mark("packs: glyph sheets");
    loadJsonUi(packs);
    timer.mark("packs: json ui");
}

/**
 * Every glyph sheet a pack ships replaces the vanilla one, the first pack in
 * the stack winning. Each glyph is cut out of its cell and stored on its own,
 * which keeps HD sheets sharp while only the glyphs they really have take
 * atlas space.
 */
void Client::loadPackGlyphs(const std::vector<std::shared_ptr<const world::PackFiles>>& packs)
{
    font.clearPixelPageGlyphs();
    for (size_t index = 0; index < ui::Font::PixelPageCount; ++index) {
        std::string name = ui::Font::pixelPageName(index);
        for (const std::shared_ptr<const world::PackFiles>& pack : packs) {
            auto encoded = pack->find(name + ".png");
            if (!encoded) {
                continue;
            }
            ui::Bitmap sheet;
            if (ui::decodeBitmap(*encoded, sheet) && sheet.width == sheet.height && sheet.width >= 16 && sheet.width % 16 == 0) {
                cutGlyphs(index, sheet);
                debugLog("pack glyph sheet " + name + " " + std::to_string(sheet.width) + "px");
            }
            break;
        }
    }
    font.reloadPixelPages();
}

void Client::cutGlyphs(size_t index, const ui::Bitmap& sheet)
{
    uint32_t cell = sheet.width / 16;
    std::array<ui::Font::GlyphBox, 256> boxes {};
    for (uint32_t code = 0; code < 256; ++code) {
        uint32_t originX = (code % 16) * cell;
        uint32_t originY = (code / 16) * cell;
        uint32_t left = cell;
        uint32_t top = cell;
        uint32_t right = 0;
        uint32_t bottom = 0;
        for (uint32_t y = 0; y < cell; ++y) {
            const uint8_t* row = sheet.rgba.data() + (static_cast<size_t>(originY + y) * sheet.width + originX) * 4;
            for (uint32_t x = 0; x < cell; ++x) {
                if (row[x * 4 + 3] > 0) {
                    left = std::min(left, x);
                    right = std::max(right, x + 1);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y + 1);
                }
            }
        }
        if (right <= left || bottom <= top) {
            continue;
        }
        ui::Bitmap glyph;
        glyph.width = right - left;
        glyph.height = bottom - top;
        glyph.rgba.resize(static_cast<size_t>(glyph.width) * glyph.height * 4);
        for (uint32_t y = 0; y < glyph.height; ++y) {
            const uint8_t* source = sheet.rgba.data() + (static_cast<size_t>(originY + top + y) * sheet.width + originX + left) * 4;
            std::copy(source, source + static_cast<size_t>(glyph.width) * 4, glyph.rgba.data() + static_cast<size_t>(y) * glyph.width * 4);
        }
        std::string sprite = ui::Font::pixelGlyphName(index, code);
        skin.setDynamic(sprite, ui::shrinkBitmap(glyph, MaxGlyphSprite));
        packSprites.push_back(std::move(sprite));
        boxes[code] = { static_cast<uint16_t>(left), static_cast<uint16_t>(top), static_cast<uint16_t>(right), static_cast<uint16_t>(bottom) };
    }
    font.setPixelPageGlyphs(index, cell, boxes);
}

namespace {

std::vector<std::string> listedUiFiles(const std::string& defs)
{
    std::vector<std::string> paths;
    if (std::unique_ptr<json::Value> root = util::parseJsonObject(defs)) {
        if (const json::Value* list = root->get("ui_defs"); list && list->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : list->mArray) {
                if (entry->isString() && !entry->mString.empty()) {
                    paths.push_back(entry->mString);
                }
            }
        }
    }
    return paths;
}

}

/**
 * Merges every UI file the game's _ui_defs.json lists with each pack's copy,
 * lowest priority pack first, plus the files packs add through their own
 * _ui_defs.json, then loads the pack textures the merged controls name.
 */
void Client::loadJsonUi(const std::vector<std::shared_ptr<const world::PackFiles>>& packs)
{
    StartupTimer timer;
    auto definitions = std::make_shared<ui::JsonUi>();
    auto readVanilla = [&](const std::string& path, std::string& text) {
        size_t slash = path.rfind('/');
        if (slash != std::string::npos && assets.readBaseArchived(path.substr(0, slash), path.substr(slash + 1), text)) {
            return true;
        }
        std::vector<unsigned char> loose = assets.readPackFile(path);
        text.assign(loose.begin(), loose.end());
        return !loose.empty();
    };
    std::string defs;
    readVanilla(UiDefsFile, defs);
    std::vector<std::string> vanillaPaths = listedUiFiles(defs);
    vanillaPaths.insert(vanillaPaths.begin(), GlobalVariablesFile);
    for (const std::string& path : vanillaPaths) {
        std::string text;
        if (readVanilla(path, text)) {
            definitions->addFile(path, text);
        }
    }
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        std::vector<std::string> paths = vanillaPaths;
        if (auto packDefs = (*pack)->find(UiDefsFile)) {
            for (std::string& path : listedUiFiles(*packDefs)) {
                if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
                    paths.push_back(std::move(path));
                }
            }
        }
        for (const std::string& path : paths) {
            if (auto text = (*pack)->find(path)) {
                definitions->addFile(path, *text);
            }
        }
    }

    std::vector<std::string> textures = definitions->texturePaths();
    timer.mark("json ui: definitions read");
    for (const std::string& texture : textures) {
        loadPackTexture(texture);
    }
    timer.mark("json ui: " + std::to_string(textures.size()) + " textures");
    menu.setJsonUi(std::move(definitions));
    timer.mark("json ui: menu screens");
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

/**
 * Puts the first copy of texture the server's packs have into the skin under
 * its own path, cut to MaxPackTexture wide with its nine slice scaled along.
 * False when no pack has it.
 */
bool Client::loadPackTexture(const std::string& texture)
{
    if (std::find(packSprites.begin(), packSprites.end(), texture) != packSprites.end()) {
        return true;
    }
    for (const std::shared_ptr<const world::PackFiles>& pack : artPacks) {
        std::shared_ptr<const std::string> encoded;
        for (const char* extension : TextureExtensions) {
            if ((encoded = pack->find(texture + extension))) {
                break;
            }
        }
        if (!encoded) {
            continue;
        }
        ui::Bitmap bitmap;
        if (!ui::decodeBitmap(*encoded, bitmap)) {
            return false;
        }
        ui::NineSlice slice;
        if (auto sliceJson = pack->find(texture + ".json")) {
            ui::readNineSlice(*sliceJson, slice);
        }
        float fit = bitmap.width > MaxPackTexture ? static_cast<float>(MaxPackTexture) / static_cast<float>(bitmap.width) : 1.0f;
        slice = { slice.left * fit, slice.top * fit, slice.right * fit, slice.bottom * fit };
        skin.setDynamic(texture, ui::shrinkBitmap(bitmap, MaxPackTexture), slice);
        packSprites.push_back(texture);
        debugLog("pack ui texture " + texture);
        return true;
    }
    return false;
}

}
