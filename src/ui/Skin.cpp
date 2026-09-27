#include "ui/Skin.h"

#include "ui/Theme.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace kestrel::ui {

namespace {

// Reads every "--name:value" custom property, which is all the menu theme stylesheet holds.
void parseTheme(const std::string& css, std::unordered_map<std::string, std::string>& out)
{
    size_t position = 0;
    while ((position = css.find("--", position)) != std::string::npos) {
        size_t colon = css.find(':', position);
        size_t end = css.find_first_of(";}", colon);
        if (colon == std::string::npos || end == std::string::npos) {
            break;
        }
        std::string name = css.substr(position + 2, colon - position - 2);
        if (name.find_first_of(" {,") == std::string::npos) {
            out.emplace(std::move(name), css.substr(colon + 1, end - colon - 1));
        }
        position = end;
    }
}

float parseLength(const std::string& token)
{
    float value = std::strtof(token.c_str(), nullptr);
    if (token.find("rem") != std::string::npos) {
        return value * theme::Rem;
    }
    return theme::css(value);
}

// CSS lists edges as top, right, bottom, left and repeats them when fewer are given.
NineSlice edges(const std::vector<float>& values)
{
    if (values.empty()) {
        return {};
    }
    float top = values[0];
    float right = values.size() > 1 ? values[1] : top;
    float bottom = values.size() > 2 ? values[2] : top;
    float left = values.size() > 3 ? values[3] : right;
    return { left, top, right, bottom };
}

std::vector<std::string> tokens(std::string_view text)
{
    std::vector<std::string> result;
    std::istringstream stream { std::string(text) };
    std::string token;
    while (stream >> token) {
        result.push_back(token);
    }
    return result;
}

// "url(/hbui/assets/pressable_elevated_secondary_default-0aacffbfa726a8184fc5.png)" becomes
// "hbui/pressable_elevated_secondary_default".
std::string spriteFromUrl(std::string_view value)
{
    size_t slash = value.rfind('/');
    size_t dash = value.rfind('-');
    if (slash == std::string_view::npos || dash == std::string_view::npos || dash < slash) {
        return {};
    }
    return "hbui/" + std::string(value.substr(slash + 1, dash - slash - 1));
}

}

Skin::Skin(GameAssets& assets)
    : assets(assets)
{
    parseTheme(assets.readHbuiText("menus-theme"), theme);
}

std::string_view Skin::themeValue(std::string_view name) const
{
    auto found = theme.find(std::string(name));
    return found == theme.end() ? std::string_view {} : std::string_view(found->second);
}

float Skin::themeLength(std::string_view name, float fallback) const
{
    std::string_view value = themeValue(name);
    return value.empty() ? fallback : parseLength(std::string(value));
}

const BorderImage& Skin::border(std::string_view component, std::string_view state)
{
    std::string key = std::string(component) + std::string(state);
    auto found = borders.find(key);
    if (found != borders.end()) {
        return found->second;
    }

    BorderImage result;
    result.sprite = spriteFromUrl(themeValue(key + "BorderImageSource"));
    std::vector<float> slice;
    for (const std::string& token : tokens(themeValue(key + "BorderImageSlice"))) {
        if (token == "fill") {
            result.fill = true;
        } else {
            slice.push_back(std::strtof(token.c_str(), nullptr));
        }
    }
    std::vector<float> width;
    for (const std::string& token : tokens(themeValue(key + "BorderImageWidth"))) {
        width.push_back(parseLength(token));
    }
    std::vector<float> outset;
    for (const std::string& token : tokens(themeValue(key + "BorderImageOutset"))) {
        outset.push_back(parseLength(token));
    }
    result.slice = edges(slice);
    result.width = edges(width);
    result.outset = edges(outset);
    result.valid = !result.sprite.empty();
    return borders.emplace(std::move(key), std::move(result)).first->second;
}

Skin::Entry& Skin::load(std::string_view name)
{
    auto found = entries.find(std::string(name));
    if (found != entries.end()) {
        return found->second;
    }

    Entry entry;
    bool loaded = false;
    if (name.rfind("ui/", 0) == 0) {
        loaded = assets.readTexture("textures/" + std::string(name), entry.bitmap, &entry.sprite.slice);
    } else if (name.rfind("hbui/", 0) == 0) {
        loaded = assets.readHbuiImage(name.substr(5), entry.bitmap);
    } else if (name.rfind("font/", 0) == 0) {
        std::string encoded;
        loaded = assets.readArchived("font", std::string(name.substr(5)) + ".png", encoded) && decodeBitmap(encoded, entry.bitmap);
    } else if (name.rfind("textures/", 0) == 0) {
        loaded = assets.readTexture(std::string(name), entry.bitmap, &entry.sprite.slice);
    }
    entry.sprite.width = static_cast<float>(entry.bitmap.width);
    entry.sprite.height = static_cast<float>(entry.bitmap.height);
    if (loaded) {
        changed = true;
    }
    return entries.emplace(std::string(name), std::move(entry)).first->second;
}

const Sprite& Skin::sprite(std::string_view name)
{
    return load(name).sprite;
}

const Bitmap* Skin::bitmap(std::string_view name)
{
    Entry& entry = load(name);
    return entry.bitmap.rgba.empty() ? nullptr : &entry.bitmap;
}

void Skin::setDynamic(const std::string& name, Bitmap bitmap)
{
    Entry& entry = entries[name];
    entry.bitmap = std::move(bitmap);
    entry.sprite = {};
    entry.sprite.width = static_cast<float>(entry.bitmap.width);
    entry.sprite.height = static_cast<float>(entry.bitmap.height);
    changed = true;
}

void Skin::clearDynamic(const std::string& name)
{
    if (entries.erase(name) > 0) {
        changed = true;
    }
}

void Skin::pack(std::vector<uint8_t>& atlasRgba)
{
    changed = false;
    std::vector<Entry*> order;
    for (auto& [name, entry] : entries) {
        entry.sprite.valid = false;
        if (!entry.bitmap.rgba.empty()) {
            order.push_back(&entry);
        }
    }
    std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
        return a->bitmap.height > b->bitmap.height;
    });

    // A one texel gutter copied from the edge keeps nearest sampling from bleeding into neighbours.
    constexpr uint32_t Gutter = 1;
    uint32_t x = 0;
    uint32_t y = ImageTop;
    uint32_t shelf = 0;
    const float extent = static_cast<float>(AtlasSize);
    for (Entry* entry : order) {
        const Bitmap& source = entry->bitmap;
        uint32_t w = source.width + Gutter * 2;
        uint32_t h = source.height + Gutter * 2;
        if (w > AtlasSize) {
            continue;
        }
        if (x + w > AtlasSize) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        if (y + h > AtlasSize) {
            break;
        }
        for (uint32_t row = 0; row < h; ++row) {
            uint32_t sourceRow = std::min(row > Gutter ? row - Gutter : 0, source.height - 1);
            for (uint32_t column = 0; column < w; ++column) {
                uint32_t sourceColumn = std::min(column > Gutter ? column - Gutter : 0, source.width - 1);
                std::memcpy(atlasRgba.data() + ((static_cast<size_t>(y) + row) * AtlasSize + x + column) * 4,
                    source.rgba.data() + (static_cast<size_t>(sourceRow) * source.width + sourceColumn) * 4, 4);
            }
        }
        Sprite& sprite = entry->sprite;
        sprite.image.u0 = (x + Gutter) / extent;
        sprite.image.v0 = (y + Gutter) / extent;
        sprite.image.u1 = (x + Gutter + source.width) / extent;
        sprite.image.v1 = (y + Gutter + source.height) / extent;
        sprite.image.valid = true;
        sprite.valid = true;
        x += w;
        shelf = std::max(shelf, h);
    }
}

}
