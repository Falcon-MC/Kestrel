#include "client/Client.h"

#include "render/Renderer.h"
#include "ui/Image.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr uint32_t MapIconColumns = 8;
constexpr float MarkerTurnDegrees = 22.5f;
constexpr float MarkerRadius = 4.0f;

}

/**
 * The entity texture layer reserved for the map background, right after the
 * dropped item icons.
 */
uint32_t Client::mapBackgroundLayer() const
{
    return heldItemLayer() + HeldItemTextureSlots + world::DroppedIconSlots;
}

/**
 * Reads the map background and marker sheet from the packs: the background
 * is scaled into its texture layer, the marker sheet kept for drawing
 * markers onto each map.
 */
void Client::loadMapArt(const std::vector<std::shared_ptr<const world::PackFiles>>& packs, uint8_t* backgroundLayer)
{
    mapIcons.clear();
    mapIconsWidth = 0;
    mapIconsHeight = 0;
    std::filesystem::path root = world::PackSource::locateVanilla();
    if (root.empty()) {
        return;
    }
    world::PackSource game(root);
    game.setOverlays(packs);
    std::string encoded;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> background;
    if (game.readTexture("textures/map/map_background", encoded) && ui::decodeImage(encoded, width, height, background) && width > 0 && height > 0) {
        uint32_t size = world::EntityTextureSize;
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                size_t source = (size_t(y * height / size) * width + x * width / size) * 4;
                std::copy_n(background.data() + source, 4, backgroundLayer + (size_t(y) * size + x) * 4);
            }
        }
    }
    encoded.clear();
    if (game.readTexture("textures/map/map_icons", encoded) && ui::decodeImage(encoded, width, height, mapIcons) && width >= MapIconColumns && height >= MapIconColumns) {
        mapIconsWidth = width;
        mapIconsHeight = height;
    } else {
        mapIcons.clear();
    }
}

/**
 * A map's texture: the pixels the server drew, and over them each marker
 * from the marker sheet, turned in sixteenths of a turn and tinted by its
 * color. Pixels the server never drew stay transparent so the background
 * shows through.
 */
std::vector<uint8_t> Client::composeMap(const MapView& map) const
{
    std::vector<uint8_t> out = map.pixels;
    if (mapIcons.empty()) {
        return out;
    }
    uint32_t cell = mapIconsWidth / MapIconColumns;
    uint32_t rows = mapIconsHeight / cell;
    for (const MapMarker& marker : map.markers) {
        if (marker.image < 0 || uint32_t(marker.image) >= MapIconColumns * rows) {
            continue;
        }
        uint32_t cellX = (uint32_t(marker.image) % MapIconColumns) * cell;
        uint32_t cellY = (uint32_t(marker.image) / MapIconColumns) * cell;
        float centerX = marker.x * 0.5f + MapView::Size * 0.5f;
        float centerY = marker.y * 0.5f + MapView::Size * 0.5f;
        float angle = marker.rotation * MarkerTurnDegrees * 3.14159265f / 180.0f;
        float cosine = std::cos(angle);
        float sine = std::sin(angle);
        float tint[3] = {
            float(marker.color & 0xFF) / 255.0f,
            float((marker.color >> 8) & 0xFF) / 255.0f,
            float((marker.color >> 16) & 0xFF) / 255.0f,
        };
        int32_t reach = int32_t(std::ceil(MarkerRadius * 1.5f));
        for (int32_t dy = -reach; dy <= reach; ++dy) {
            for (int32_t dx = -reach; dx <= reach; ++dx) {
                int32_t px = int32_t(std::floor(centerX)) + dx;
                int32_t py = int32_t(std::floor(centerY)) + dy;
                if (px < 0 || py < 0 || px >= MapView::Size || py >= MapView::Size) {
                    continue;
                }
                float offsetX = px + 0.5f - centerX;
                float offsetY = py + 0.5f - centerY;
                float iconX = cosine * offsetX + sine * offsetY;
                float iconY = -sine * offsetX + cosine * offsetY;
                float u = (iconX / (MarkerRadius * 2.0f) + 0.5f) * float(cell);
                float v = (iconY / (MarkerRadius * 2.0f) + 0.5f) * float(cell);
                if (u < 0.0f || v < 0.0f || u >= float(cell) || v >= float(cell)) {
                    continue;
                }
                const uint8_t* texel = mapIcons.data() + (size_t(cellY + uint32_t(v)) * mapIconsWidth + cellX + uint32_t(u)) * 4;
                if (texel[3] < 128) {
                    continue;
                }
                uint8_t* pixel = out.data() + (size_t(py) * MapView::Size + size_t(px)) * 4;
                for (size_t channel = 0; channel < 3; ++channel) {
                    pixel[channel] = static_cast<uint8_t>(std::clamp(texel[channel] * tint[channel], 0.0f, 255.0f));
                }
                pixel[3] = 255;
            }
        }
    }
    return out;
}

/**
 * What makes a held mesh of this item distinct: for a filled map, the map
 * and the revision of its content, asking the server for it when unknown.
 */
std::string Client::mapMeshKey(const HudItem& item)
{
    if (item.identifier != "minecraft:filled_map" || item.mapId == 0) {
        return std::string();
    }
    return "#map:" + std::to_string(item.mapId) + ":" + std::to_string(session.mapRevision(item.mapId));
}

}
