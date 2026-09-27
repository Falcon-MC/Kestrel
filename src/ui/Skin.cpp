#include "ui/Skin.h"

#include "ui/Theme.h"

#include "TitlePng.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace kestrel::ui {

namespace {

constexpr uint32_t TitleWidth = 800;

/**
 * Shrinks a bitmap to the given width with a box filter over premultiplied
 * colors, keeping its aspect ratio.
 */
Bitmap shrink(const Bitmap& source, uint32_t width)
{
    if (source.width <= width) {
        return source;
    }
    Bitmap out;
    out.width = width;
    out.height = std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(source.height) * width / source.width));
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    for (uint32_t y = 0; y < out.height; ++y) {
        uint32_t y0 = y * source.height / out.height;
        uint32_t y1 = std::max(y0 + 1, (y + 1) * source.height / out.height);
        for (uint32_t x = 0; x < out.width; ++x) {
            uint32_t x0 = x * source.width / out.width;
            uint32_t x1 = std::max(x0 + 1, (x + 1) * source.width / out.width);
            double sum[4] {};
            for (uint32_t sy = y0; sy < y1; ++sy) {
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = source.rgba.data() + (static_cast<size_t>(sy) * source.width + sx) * 4;
                    double alpha = p[3] / 255.0;
                    sum[0] += p[0] * alpha;
                    sum[1] += p[1] * alpha;
                    sum[2] += p[2] * alpha;
                    sum[3] += p[3];
                }
            }
            double count = static_cast<double>((x1 - x0) * (y1 - y0));
            double alpha = sum[3] / count / 255.0;
            uint8_t* q = out.rgba.data() + (static_cast<size_t>(y) * out.width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                q[c] = alpha > 0.0 ? static_cast<uint8_t>(std::clamp(sum[c] / count / alpha, 0.0, 255.0)) : 0;
            }
            q[3] = static_cast<uint8_t>(std::clamp(sum[3] / count, 0.0, 255.0));
        }
    }
    return out;
}

}

Bitmap shrinkBitmap(const Bitmap& source, uint32_t width)
{
    return shrink(source, width);
}

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
    if (name == "kestrel/title") {
        std::string encoded(reinterpret_cast<const char*>(KestrelTitleData::kTitlePng), KestrelTitleData::kTitlePngSize);
        Bitmap decoded;
        loaded = decodeBitmap(encoded, decoded);
        if (loaded) {
            entry.bitmap = shrink(decoded, TitleWidth);
        }
    } else if (name.rfind("ui/", 0) == 0) {
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

void Skin::setDynamic(const std::string& name, Bitmap bitmap, NineSlice slice)
{
    Entry& entry = entries[name];
    reclaimable = reclaimable || entry.placed;
    entry.bitmap = std::move(bitmap);
    entry.sprite = {};
    entry.sprite.slice = slice;
    entry.sprite.width = static_cast<float>(entry.bitmap.width);
    entry.sprite.height = static_cast<float>(entry.bitmap.height);
    entry.placed = false;
    changed = true;
}

void Skin::clearDynamic(const std::string& name)
{
    auto found = entries.find(name);
    if (found != entries.end()) {
        reclaimable = reclaimable || found->second.placed;
        entries.erase(found);
        changed = true;
    }
}

bool Skin::place(Entry& entry)
{
    uint32_t w = entry.bitmap.width + 2;
    uint32_t h = entry.bitmap.height + 2;
    if (w > AtlasSize) {
        return false;
    }
    if (cursorX + w > AtlasSize) {
        cursorX = 0;
        cursorY += shelfHeight;
        shelfHeight = 0;
    }
    if (cursorY + h > AtlasSize) {
        return false;
    }
    entry.x = cursorX;
    entry.y = cursorY;
    entry.placed = true;
    cursorX += w;
    shelfHeight = std::max(shelfHeight, h);
    return true;
}

void Skin::pack(std::vector<uint8_t>& atlasRgba)
{
    changed = false;
    auto unplaced = [&]() {
        std::vector<Entry*> order;
        for (auto& [name, entry] : entries) {
            if (!entry.placed && !entry.bitmap.rgba.empty()) {
                order.push_back(&entry);
            }
        }
        std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
            return a->bitmap.height > b->bitmap.height;
        });
        return order;
    };
    bool full = false;
    for (Entry* entry : unplaced()) {
        if (!place(*entry) && entry->bitmap.width + 2 <= AtlasSize) {
            full = true;
            break;
        }
    }
    if (full && reclaimable) {
        reclaimable = false;
        cursorX = 0;
        cursorY = ImageTop;
        shelfHeight = 0;
        for (auto& [name, entry] : entries) {
            entry.placed = false;
        }
        for (Entry* entry : unplaced()) {
            place(*entry);
        }
    }

    // A one texel gutter copied from the edge keeps nearest sampling from bleeding into neighbours.
    constexpr uint32_t Gutter = 1;
    const float extent = static_cast<float>(AtlasSize);
    for (auto& [name, slot] : entries) {
        Entry* entry = &slot;
        entry->sprite.valid = false;
        if (!entry->placed || entry->bitmap.rgba.empty()) {
            continue;
        }
        const Bitmap& source = entry->bitmap;
        uint32_t w = source.width + Gutter * 2;
        uint32_t h = source.height + Gutter * 2;
        uint32_t x = entry->x;
        uint32_t y = entry->y;
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
    }
}

}
