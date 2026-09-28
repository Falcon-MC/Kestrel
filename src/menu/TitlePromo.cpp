#include "menu/Menu.h"

#include "Core/Json/Json.h"
#include "platform/Paths.h"
#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/GameAssets.h"
#include "ui/Localization.h"
#include "ui/Skin.h"
#include "util/JsonText.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace kestrel::menu {

using namespace ui;

namespace {

constexpr const char* BadgeSprite = "dynamic/promo_badge";
constexpr const char* CaptionSprite = "dynamic/promo_caption";
constexpr const char* PromoLink = "https://www.minecraft.net/en-us/about-dungeons-ii?OCID=inGameHomeScreen";
constexpr float PromoWidth = 100.0f;
constexpr float BadgeHeight = 30.0f;
constexpr float ButtonHeight = 25.0f;
constexpr float CaptionLabelWidth = 88.0f;
constexpr float CaptionWidth = CaptionLabelWidth - 8.0f;
constexpr float CaptionPadding = 22.0f;
constexpr float CaptionLabelOffset = 4.0f;

bool readFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

std::unique_ptr<json::Value> readJson(const std::filesystem::path& path)
{
    std::string text;
    if (!readFile(path, text)) {
        return nullptr;
    }
    return json::parse(util::stripJsonComments(text));
}

/**
 * Visits every object of a UI file, however deep its controls nest.
 */
void eachObject(const json::Value& value, const std::function<void(const json::Value&)>& visit)
{
    if (value.isObject()) {
        visit(value);
        for (const std::string& key : value.mKeys) {
            eachObject(*value.get(key), visit);
        }
    } else if (value.isArray()) {
        for (const std::unique_ptr<json::Value>& element : value.mArray) {
            eachObject(*element, visit);
        }
    }
}

const std::string* stringOf(const json::Value& object, const char* key)
{
    const json::Value* value = object.get(key);
    return value && value->isString() ? &value->mString : nullptr;
}

/**
 * The treatment pack whose start screen plays a flip book badge, found
 * among the ones the game has downloaded: its frames and pace, the badge
 * strip and caption flyout put in the skin, and the caption text.
 */
bool loadPromo(Skin& skin, TitlePromo& promo)
{
    std::filesystem::path root = platform::dataDirectory().parent_path() / "Minecraft Bedrock" / "treatments" / "treatment_packs2";
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return false;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, error)) {
        std::unique_ptr<json::Value> screen = readJson(entry.path() / "ui" / "start_screen.json");
        if (!screen) {
            continue;
        }
        const json::Value* flipBook = nullptr;
        std::string badgeTexture;
        std::string captionTexture;
        std::string caption;
        eachObject(*screen, [&](const json::Value& object) {
            const std::string* anim = stringOf(object, "anim_type");
            if (anim && *anim == "flip_book") {
                flipBook = &object;
            }
            const std::string* texture = stringOf(object, "texture");
            if (texture && object.get("uv_size")) {
                badgeTexture = *texture;
            }
            if (texture && object.get("controls") && caption.empty()) {
                eachObject(*object.get("controls"), [&](const json::Value& child) {
                    const std::string* type = stringOf(child, "type");
                    const std::string* text = stringOf(child, "text");
                    if (type && *type == "label" && text && caption.empty()) {
                        caption = *text;
                        captionTexture = *texture;
                    }
                });
            }
        });
        if (!flipBook || badgeTexture.empty()) {
            continue;
        }
        std::string strip;
        Bitmap badge;
        if (!readFile(entry.path() / (badgeTexture + ".png"), strip) || !decodeBitmap(strip, badge)) {
            continue;
        }
        const json::Value* frames = flipBook->get("frame_count");
        const json::Value* fps = flipBook->get("fps");
        promo.frames = frames && frames->isNumber() ? std::max<uint32_t>(1, static_cast<uint32_t>(frames->number())) : 1;
        promo.fps = fps && fps->isNumber() ? std::max(0.1f, static_cast<float>(fps->number())) : 1.0f;
        promo.frameWidth = static_cast<float>(badge.width) / static_cast<float>(promo.frames);
        promo.frameHeight = static_cast<float>(badge.height);
        promo.caption = caption;
        skin.setDynamic(BadgeSprite, std::move(badge));

        std::string flyout;
        Bitmap flyoutBitmap;
        if (!captionTexture.empty() && readFile(entry.path() / (captionTexture + ".png"), flyout) && decodeBitmap(flyout, flyoutBitmap)) {
            NineSlice slice;
            if (std::unique_ptr<json::Value> meta = readJson(entry.path() / (captionTexture + ".json"))) {
                const json::Value* size = meta->get("nineslice_size");
                if (size && size->isArray() && size->mArray.size() == 4) {
                    slice = { static_cast<float>(size->mArray[0]->number()), static_cast<float>(size->mArray[1]->number()),
                        static_cast<float>(size->mArray[2]->number()), static_cast<float>(size->mArray[3]->number()) };
                }
            }
            skin.setDynamic(CaptionSprite, std::move(flyoutBitmap), slice);
        }
        return true;
    }
    return false;
}

/**
 * Breaks a caption into the lines a label of the given width shows.
 */
std::vector<std::string> wrap(const Context& ui, const std::string& text, float width)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui.measure(candidate, TextStyle::Pixel) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = std::move(candidate);
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

}

/**
 * The start screen promotion stacked the way its UI file lays it out: the
 * caption flyout over the animated badge, the learn more button under them,
 * all above the corner buttons.
 */
void Menu::titlePromo(Context& ui, float left, float bottom)
{
    if (!promo.loaded) {
        promo.loaded = true;
        promo.valid = loadPromo(ui.skin(), promo);
    }
    if (!promo.valid) {
        return;
    }
    float x = std::floor(left);
    float buttonY = bottom - ButtonHeight;
    float badgeY = buttonY - BadgeHeight;

    float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count();
    uint32_t frame = static_cast<uint32_t>(seconds * promo.fps) % promo.frames;
    float badgeWidth = PromoWidth;
    float badgeHeight = promo.frameWidth > 0.0f ? std::min(BadgeHeight, PromoWidth * promo.frameHeight / promo.frameWidth) : BadgeHeight;
    ui.spriteRegion({ x, std::floor(badgeY + (BadgeHeight - badgeHeight) * 0.5f), badgeWidth, badgeHeight }, BadgeSprite,
        { frame * promo.frameWidth, 0.0f, promo.frameWidth, promo.frameHeight });

    if (!promo.caption.empty()) {
        std::vector<std::string> lines = wrap(ui, promo.caption, CaptionLabelWidth);
        float lineHeight = ui.lineHeight(TextStyle::Pixel);
        float labelHeight = lineHeight * static_cast<float>(lines.size());
        float captionHeight = labelHeight + CaptionPadding;
        Rect flyout { std::floor(x + (PromoWidth - CaptionWidth) * 0.5f), badgeY - captionHeight + 4.0f, CaptionWidth, captionHeight };
        ui.nineSlice(flyout, CaptionSprite);
        float labelTop = std::floor(flyout.y + (flyout.h - labelHeight) * 0.5f + CaptionLabelOffset * 0.5f);
        for (size_t line = 0; line < lines.size(); ++line) {
            ui.textCentered(lines[line], TextStyle::Pixel, { x + (PromoWidth - CaptionLabelWidth) * 0.5f, labelTop + lineHeight * static_cast<float>(line), CaptionLabelWidth, lineHeight }, { 255, 255, 255, 255 });
        }
    }

    if (ui.classicButton("title:promo", tr("selectWorld.learnMore", "Learn More"), { x, buttonY, PromoWidth, ButtonHeight })) {
        platform::openUrl(PromoLink);
    }
}

}
