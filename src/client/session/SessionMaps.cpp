#include "client/Session.h"

#include "Protocol/Packets/ClientboundMapItemDataPacket.h"
#include "Protocol/Packets/MapInfoRequestPacket.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr size_t MaxKnownMaps = 512;
constexpr size_t MaxMarkers = 128;
constexpr uint32_t FrameScanInterval = 10;
constexpr int32_t FrameScanChunks = 4;
constexpr int32_t FrameScanSections = 3;
constexpr size_t MaxFrames = 256;

/**
 * The stack an item frame's block entity holds, as its Item compound
 * describes it: name, count, data value, enchantments and the map it shows.
 */
HudItem frameItem(const Tag& item)
{
    HudItem out;
    out.identifier = item.getString("Name", "");
    if (out.identifier.empty()) {
        return out;
    }
    const Tag* count = item.get("Count");
    out.count = count && count->getType() == Tag::Type::Byte ? count->asByte() : 1;
    if (out.count <= 0) {
        out.count = 1;
    }
    const Tag* damage = item.get("Damage");
    out.aux = damage && damage->getType() == Tag::Type::Short ? damage->asShort() : 0;
    if (const Tag* tag = item.get("tag"); tag && tag->isCompound()) {
        if (const Tag* map = tag->get("map_uuid"); map && map->getType() == Tag::Type::Long) {
            out.mapId = map->asLong();
        }
        if (const Tag* enchantments = tag->get("ench"); enchantments && enchantments->isList()) {
            out.enchanted = !enchantments->getList().empty();
        }
    }
    return out;
}

}

/**
 * Collects, every half second, the items held by the item frames around the
 * player, read from their block entities and the frame's facing state.
 */
void Session::tickFrameItems()
{
    if (++frameScanTicks < FrameScanInterval || !assets) {
        return;
    }
    frameScanTicks = 0;
    MotionVector feet = motion.position();
    int32_t chunkX = static_cast<int32_t>(std::floor(feet.x)) >> 4;
    int32_t chunkY = static_cast<int32_t>(std::floor(feet.y)) >> 4;
    int32_t chunkZ = static_cast<int32_t>(std::floor(feet.z)) >> 4;
    std::vector<FrameItemView> frames;
    for (int32_t x = chunkX - FrameScanChunks; x <= chunkX + FrameScanChunks && frames.size() < MaxFrames; ++x) {
        for (int32_t z = chunkZ - FrameScanChunks; z <= chunkZ + FrameScanChunks && frames.size() < MaxFrames; ++z) {
            for (int32_t y = chunkY - FrameScanSections; y <= chunkY + FrameScanSections && frames.size() < MaxFrames; ++y) {
                std::shared_ptr<const world::BlockEntityMap> entities = world.store().blockEntities({ current.dimension, x, y, z });
                if (!entities) {
                    continue;
                }
                for (const auto& [index, data] : *entities) {
                    std::string id = data.getString("id", "");
                    if (id != "ItemFrame" && id != "GlowItemFrame") {
                        continue;
                    }
                    const Tag* item = data.get("Item");
                    if (!item || !item->isCompound()) {
                        continue;
                    }
                    FrameItemView view;
                    view.cell = { x * 16 + int32_t((index >> 8) & 15), y * 16 + int32_t(index & 15), z * 16 + int32_t((index >> 4) & 15) };
                    view.item = frameItem(*item);
                    if (view.item.empty()) {
                        continue;
                    }
                    if (const Tag* rotation = data.get("ItemRotation")) {
                        view.rotation = rotation->getType() == Tag::Type::Float ? rotation->asFloat() : rotation->getType() == Tag::Type::Byte ? rotation->asByte() * 45.0f : 0.0f;
                    }
                    uint32_t value = blockAt(view.cell[0], view.cell[1], view.cell[2]);
                    if (const Tag* states = assets->blockStates(value, ids.hashed, ids.sequential.get()); states && states->isCompound()) {
                        if (const Tag* facing = states->get("facing_direction"); facing && facing->getType() == Tag::Type::Int) {
                            view.facing = facing->asInt();
                        }
                    }
                    frames.push_back(std::move(view));
                    if (frames.size() >= MaxFrames) {
                        break;
                    }
                }
            }
        }
    }
    std::lock_guard<std::mutex> guard(mutex);
    current.frameItems = std::move(frames);
}

/**
 * Takes in what the server says about a map: a block of pixels written at
 * an offset, given as ABGR words, and the markers, which replace the old
 * ones whenever they are sent. Each change bumps the map's revision so the
 * renderer knows to redraw it.
 */
void Session::handleMapPacket(const ClientboundMapItemDataPacket& packet)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto found = maps.find(packet.mUniqueMapId);
    if (found == maps.end()) {
        if (maps.size() >= MaxKnownMaps) {
            return;
        }
        found = maps.emplace(packet.mUniqueMapId, MapView {}).first;
    }
    MapView& map = found->second;
    bool changed = false;
    if (packet.mHasColors && packet.mHasWidth && packet.mHasHeight) {
        int32_t width = packet.mWidth;
        int32_t height = packet.mHeight;
        int32_t startX = packet.mHasXOffset ? packet.mXOffset : 0;
        int32_t startY = packet.mHasYOffset ? packet.mYOffset : 0;
        if (width > 0 && height > 0 && size_t(width) * size_t(height) <= packet.mColors.size()) {
            for (int32_t y = 0; y < height; ++y) {
                int32_t row = startY + y;
                if (row < 0 || row >= MapView::Size) {
                    continue;
                }
                for (int32_t x = 0; x < width; ++x) {
                    int32_t column = startX + x;
                    if (column < 0 || column >= MapView::Size) {
                        continue;
                    }
                    uint32_t abgr = static_cast<uint32_t>(packet.mColors[size_t(y) * size_t(width) + size_t(x)]);
                    uint8_t* pixel = map.pixels.data() + (size_t(row) * MapView::Size + size_t(column)) * 4;
                    pixel[0] = static_cast<uint8_t>(abgr & 0xFF);
                    pixel[1] = static_cast<uint8_t>((abgr >> 8) & 0xFF);
                    pixel[2] = static_cast<uint8_t>((abgr >> 16) & 0xFF);
                    pixel[3] = static_cast<uint8_t>((abgr >> 24) & 0xFF);
                }
            }
            changed = true;
        }
    }
    if (packet.mHasDecorations) {
        map.markers.clear();
        for (const MapDecoration& decoration : packet.mDecorations) {
            if (map.markers.size() >= MaxMarkers) {
                break;
            }
            MapMarker marker;
            marker.image = decoration.mImage;
            marker.rotation = decoration.mRotation & 15;
            marker.x = static_cast<int8_t>(decoration.mXOffset);
            marker.y = static_cast<int8_t>(decoration.mYOffset);
            marker.color = static_cast<uint32_t>(decoration.mColor);
            map.markers.push_back(marker);
        }
        changed = true;
    }
    if (changed || map.revision == 0) {
        map.revision = ++mapRevisions;
    }
}

uint64_t Session::mapRevision(int64_t mapId)
{
    if (mapId == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (auto found = maps.find(mapId); found != maps.end()) {
        return found->second.revision;
    }
    if (requestedMaps.size() < MaxKnownMaps && requestedMaps.insert(mapId).second) {
        pendingMapRequests.push_back(mapId);
    }
    return 0;
}

bool Session::copyMap(int64_t mapId, MapView& out)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto found = maps.find(mapId);
    if (found == maps.end()) {
        return false;
    }
    out = found->second;
    return true;
}

/**
 * Asks the server for every map the client wanted to show but has not
 * heard of, once each, the way the game requests a map it sees in a hand or
 * a frame.
 */
void Session::sendMapRequests()
{
    std::vector<int64_t> requests;
    {
        std::lock_guard<std::mutex> guard(mutex);
        requests.swap(pendingMapRequests);
    }
    if (!connection) {
        return;
    }
    for (int64_t mapId : requests) {
        MapInfoRequestPacket request;
        request.mUniqueMapId = mapId;
        transmit(request);
    }
}

}
