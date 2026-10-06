#include "client/session/SessionData.h"
#include "client/ActorEquipment.h"

#include "Core/Json/Json.h"
#include "Core/NBT/NbtIo.h"
#include "Protocol/BlockStateHasher.h"
#include "util/SkinChoice.h"
#include "util/Text.h"
#include "BlockUpgradeSchemas.h"
#include "Core/BlockState/BlockStateUpgrader.h"
#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/BedrockConnection.h"
#include "Network/Http/HttpClient.h"
#include "Network/Client/ClientNetworkSystem.h"
#include "Network/Session/MultiplayerSessionDirectory.h"
#include "Network/Session/RealmsService.h"
#include "client/DebugLog.h"
#include "Network/Crypto/Base64.h"
#include "ui/Image.h"
#include "world/PackSource.h"
#include "Protocol/Packets/AddActorPacket.h"
#include "Protocol/Packets/AnimatePacket.h"
#include "Protocol/Packets/AddPlayerPacket.h"
#include "Protocol/Packets/BlockActorDataPacket.h"
#include "Protocol/Packets/CameraInstructionPacket.h"
#include "Protocol/Packets/CameraPresetsPacket.h"
#include "Protocol/Packets/CameraShakePacket.h"
#include "Protocol/Packets/ChangeDimensionPacket.h"
#include "Protocol/Packets/PlayerActionPacket.h"
#include "Protocol/Packets/AddItemActorPacket.h"
#include "Protocol/Packets/BlockEventPacket.h"
#include "Protocol/Packets/TakeItemActorPacket.h"
#include "Protocol/Packets/ChunkRadiusUpdatedPacket.h"
#include "Protocol/Packets/DimensionDataPacket.h"
#include "Protocol/Packets/GameRulesChangedPacket.h"
#include "Protocol/Packets/LevelEventPacket.h"
#include "Protocol/Packets/MoveActorAbsolutePacket.h"
#include "Protocol/Packets/MoveActorDeltaPacket.h"
#include "Protocol/Packets/PlayerListPacket.h"
#include "Protocol/Packets/PlayerSkinPacket.h"
#include "Protocol/Packets/RemoveActorPacket.h"
#include "Protocol/Packets/PacketViolationWarningPacket.h"
#include "Protocol/Packets/PlayStatusPacket.h"
#include "Protocol/Packets/InventoryContentPacket.h"
#include "Protocol/Packets/InventorySlotPacket.h"
#include "Protocol/Packets/ItemRegistryPacket.h"
#include "Protocol/Packets/MobEffectPacket.h"
#include "Protocol/Packets/MobArmorEquipmentPacket.h"
#include "Protocol/Packets/MobEquipmentPacket.h"
#include "Protocol/Packets/PlayerHotbarPacket.h"
#include "Protocol/Packets/RequestChunkRadiusPacket.h"
#include "Protocol/Packets/SetHealthPacket.h"
#include "Protocol/Packets/ActorEventPacket.h"
#include "Protocol/Packets/SetPlayerGameTypePacket.h"
#include "Protocol/Packets/UpdatePlayerGameTypePacket.h"
#include "Protocol/Packets/UpdateAttributesPacket.h"
#include "Protocol/Packets/SetActorDataPacket.h"
#include "Protocol/Packets/SetActorLinkPacket.h"
#include "Protocol/Packets/ServerboundLoadingScreenPacket.h"
#include "Protocol/Packets/SetLocalPlayerAsInitializedPacket.h"
#include "Protocol/Packets/SetTimePacket.h"
#include "Protocol/Packets/PlayerFogPacket.h"
#include "Protocol/Packets/LevelChunkPacket.h"
#include "Protocol/Packets/ClientboundMapItemDataPacket.h"
#include "Protocol/Packets/MovePlayerPacket.h"
#include "Protocol/Packets/NetworkChunkPublisherUpdatePacket.h"
#include "Protocol/Packets/StartGamePacket.h"
#include "Protocol/Packets/SubChunkPacket.h"
#include "Protocol/Packets/SubChunkRequestPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"
#include "Protocol/Packets/UpdateBlockSyncedPacket.h"
#include "Protocol/Packets/UpdateSubChunkBlocksPacket.h"
#include "Protocol/Packets/CorrectPlayerMovePredictionPacket.h"
#include "Protocol/Packets/NetworkStackLatencyPacket.h"
#include "Protocol/Packets/PlayerAuthInputPacket.h"
#include "Protocol/Packets/RespawnPacket.h"
#include "Protocol/Packets/LevelSoundEventPacket.h"
#include "Protocol/Packets/PlaySoundPacket.h"
#include "Protocol/Packets/StopSoundPacket.h"
#include "Protocol/Packets/SetActorMotionPacket.h"
#include "Protocol/Packets/TransferPacket.h"
#include "Protocol/Packets/UpdateAbilitiesPacket.h"
#include "Protocol/Packets/SpawnParticleEffectPacket.h"
#include "client/ParticleTriggers.h"
#include "world/BlockCollisions.h"

#include "platform/Paths.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "world/ServerPack.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <exception>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace kestrel {

double secondsNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double currentWorldTime(const SessionSnapshot& snapshot)
{
    return worldTimeAt(snapshot.worldTime, snapshot.worldTimeStamp, snapshot.daylightCycle, snapshot.worldClockPaused, secondsNow());
}

namespace {

const BlockStateUpgrader& blockStateUpgrader()
{
    static const BlockStateUpgrader upgrader = [] {
        std::vector<BlockStateUpgradeSchema> schemas;
        for (const KestrelBlockUpgradeSchemas::Schema& schema : KestrelBlockUpgradeSchemas::kSchemas) {
            schemas.push_back(BlockStateUpgradeSchema::fromJson(std::string(reinterpret_cast<const char*>(schema.data), schema.size), schema.id));
        }
        return BlockStateUpgrader(std::move(schemas));
    }();
    return upgrader;
}

constexpr int ProtocolVersion = 2193;
constexpr const char* GameVersion = "1.26.51";
constexpr const char* RealmPrefix = "realm_id/";
constexpr const char* ExperiencePrefix = "experience_id/";
constexpr const char* SessionHandlePrefix = "session_handle/";
constexpr unsigned int TimeoutMs = 30000;
// The block outline follows the crosshair, so the loop never sleeps long enough for it to lag behind.
constexpr int OutlineRefreshMs = 10;
// How long a sub-chunk at the loading edge waits for its neighbour columns before it is meshed anyway.
constexpr double FrontierWaitSeconds = 1.0;
constexpr double FrontierRecheckSeconds = 0.1;

/**
 * Sends the game's Steve texture as the player's skin, so other players and
 * the first person arm see Steve instead of a blank skin.
 */
void applyDefaultSkin(ClientData& identity)
{
    std::filesystem::path vanilla = world::PackSource::locateVanilla();
    if (vanilla.empty()) {
        return;
    }
    world::PackSource pack(vanilla);
    util::SkinChoice choice = util::loadSkinChoice();
    std::string encoded;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
    bool loaded = (choice.kind == "custom" && util::readCustomSkin(width, height, rgba))
        || (choice.kind.rfind("default:", 0) == 0 && util::readDefaultSkin(choice.kind.substr(8), width, height, rgba));
    if (!loaded) {
        std::string texture = choice.kind == "alex" ? "textures/entity/alex" : "textures/entity/steve";
        if (!pack.readTexture(texture, encoded) || !ui::decodeImage(encoded, width, height, rgba) || width != 64 || (height != 64 && height != 32)) {
            return;
        }
    }
    if (choice.slim) {
        identity.mSkinResourcePatch = Base64::encode("{\"geometry\":{\"default\":\"geometry.humanoid.customSlim\"}}");
    }
    identity.mSkinData = Base64::encode(std::string(rgba.begin(), rgba.end()));
    identity.mSkinImageWidth = static_cast<int>(width);
    identity.mSkinImageHeight = static_cast<int>(height);
}

using session::applyActorMetadata;
using session::EyeHeight;
using session::metadataScale;
using session::PlayerEyeHeight;

const char* gameModeName(GameType type)
{
    switch (type) {
    case GameType::Survival:
        return "Survival";
    case GameType::Creative:
        return "Creative";
    case GameType::Adventure:
        return "Adventure";
    case GameType::Spectator:
        return "Spectator";
    case GameType::SurvivalViewer:
    case GameType::CreativeViewer:
        return "Viewer";
    case GameType::Default:
        break;
    }
    return "Default";
}

int32_t floorChunk(float blockCoordinate)
{
    return static_cast<int32_t>(std::floor(blockCoordinate / 16.0f));
}

std::filesystem::path packDirectory()
{
    return platform::dataDirectory() / "packs";
}

std::filesystem::path packPath(const std::string& id, const std::string& version)
{
    auto component = [](const std::string& value) {
        std::string safe;
        constexpr char Hex[] = "0123456789abcdef";
        for (unsigned char c : value) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.') safe += char(c);
            else { safe += '%'; safe += Hex[c >> 4]; safe += Hex[c & 15]; }
        }
        return safe;
    };
    return packDirectory() / (component(id) + "_" + component(version) + ".zip");
}

bool packMatches(const world::PackFiles& pack, const ResourcePackOffer& offer)
{
    auto manifest = pack.find("manifest.json");
    if (!manifest) manifest = pack.find("pack_manifest.json");
    auto document = manifest ? json::parse(*manifest) : nullptr;
    const auto* header = document ? document->get("header") : nullptr;
    const auto* uuid = header ? header->get("uuid") : nullptr;
    const auto* version = header ? header->get("version") : nullptr;
    if (!uuid || !uuid->isString() || !version) return false;
    auto lower = [](std::string value) {
        for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return value;
    };
    if (lower(uuid->string()) != lower(offer.mPackId)) return false;
    std::string text = version->isString() ? version->string() : std::string();
    if (version->isArray()) {
        for (const auto& part : version->mArray) {
            if (!part->isNumber()) return false;
            if (!text.empty()) text += '.';
            text += std::to_string(part->integer());
        }
    }
    return text == offer.mPackVersion;
}

bool savePack(const DownloadedResourcePack& pack, std::string& message)
{
    std::error_code error;
    std::filesystem::create_directories(packDirectory(), error);
    if (error) { message = "cannot create resource pack cache: " + error.message(); return false; }
    auto path = packPath(pack.mOffer.mPackId, pack.mOffer.mPackVersion);
    auto temporary = path;
    temporary += ".part";
    {
        std::ofstream archive(temporary, std::ios::binary | std::ios::trunc);
        archive.write(pack.mData.data(), static_cast<std::streamsize>(pack.mData.size()));
        archive.close();
        if (!archive) { message = "cannot write resource pack cache"; std::filesystem::remove(temporary, error); return false; }
    }
    auto keyPath = path;
    keyPath.replace_extension(".key");
    {
        std::ofstream key(keyPath, std::ios::trunc);
        key << pack.mOffer.mContentKey;
        key.close();
        if (!key) { message = "cannot write resource pack key"; std::filesystem::remove(temporary, error); return false; }
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) { message = "cannot publish resource pack cache: " + error.message(); return false; }
    return true;
}

std::shared_ptr<const std::vector<uint8_t>> packTitle(const world::PackFiles& pack)
{
    auto encoded = pack.find("textures/ui/title.png");
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!encoded || !ui::decodeImage(*encoded, width, height, rgba) || width == 0 || height == 0) {
        return nullptr;
    }
    constexpr uint32_t SlotWidth = ui::Font::TitleWidth;
    constexpr uint32_t SlotHeight = ui::Font::TitleHeight;
    float fit = std::min(float(SlotWidth) / float(width), float(SlotHeight) / float(height));
    uint32_t drawnWidth = std::max<uint32_t>(1, uint32_t(float(width) * fit));
    uint32_t drawnHeight = std::max<uint32_t>(1, uint32_t(float(height) * fit));
    uint32_t left = (SlotWidth - drawnWidth) / 2;
    uint32_t top = (SlotHeight - drawnHeight) / 2;
    auto slot = std::make_shared<std::vector<uint8_t>>(size_t(SlotWidth) * SlotHeight * 4, 0);
    for (uint32_t y = 0; y < drawnHeight; ++y) {
        for (uint32_t x = 0; x < drawnWidth; ++x) {
            uint32_t sx0 = x * width / drawnWidth;
            uint32_t sx1 = std::max(sx0 + 1, (x + 1) * width / drawnWidth);
            uint32_t sy0 = y * height / drawnHeight;
            uint32_t sy1 = std::max(sy0 + 1, (y + 1) * height / drawnHeight);
            uint32_t sum[4] = {};
            uint32_t count = 0;
            for (uint32_t sy = sy0; sy < sy1; ++sy) {
                for (uint32_t sx = sx0; sx < sx1; ++sx) {
                    const uint8_t* texel = rgba.data() + (size_t(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += texel[c];
                    }
                    ++count;
                }
            }
            uint8_t* out = slot->data() + (size_t(top + y) * SlotWidth + left + x) * 4;
            for (int c = 0; c < 4; ++c) {
                out[c] = static_cast<uint8_t>(sum[c] / count);
            }
        }
    }
    return slot;
}

/**
 * Partner servers are joined the way the game joins them: the gatherings
 * service hands out the address on every join. Creator experiences have no
 * other address, and featured servers may be steered away from their catalog
 * one (The Hive answers with a different host).
 */
bool resolveExperience(MinecraftAuthentication& authentication, const std::string& experienceId, std::string& host, unsigned short& port, std::string& error)
{
    std::string serviceUri;
    std::string authorization;
    if (!authentication.requestServiceUri("gatherings", serviceUri, error) || !authentication.requestServiceToken(authorization, error)) {
        return false;
    }
    HttpClient::Headers headers { { "Content-Type", "application/json" }, { "Accept", "application/json" }, { "Authorization", authorization } };
    HttpResponse response;
    if (!HttpClient::post(serviceUri + "/api/v2.0/join/experience", headers, "{\"experienceId\":\"" + json::escape(experienceId) + "\"}", response, error, TimeoutMs)) {
        return false;
    }
    std::unique_ptr<json::Value> root = json::parse(response.mBody);
    const json::Value* result = root ? root->get("result") : nullptr;
    const json::Value* address = result ? result->get("ipV4Address") : nullptr;
    const json::Value* joinPort = result ? result->get("port") : nullptr;
    if (response.mStatus != 200 || !address || address->string().empty() || !joinPort) {
        error = "Could not join the experience (status " + std::to_string(response.mStatus) + ")";
        return false;
    }
    host = address->string();
    port = static_cast<unsigned short>(joinPort->number());
    return true;
}

/**
 * Whether joining the target first goes through an online service to find
 * where the world is: a Realm, its invite link, a featured server or a
 * friend's multiplayer session.
 */
bool resolvesOnline(const std::string& target)
{
    return target.rfind(RealmPrefix, 0) == 0 || target.rfind(ExperiencePrefix, 0) == 0 || target.rfind(SessionHandlePrefix, 0) == 0 || !RealmsService::inviteCode(target).empty();
}

/**
 * Accepts a Realm invite code the way the game does when joining through a
 * link, which makes the player a member, and gives the Realm's id.
 */
bool acceptRealmInvite(RealmsService& realms, const std::string& code, long long& realmId, std::string& error)
{
    RealmDescription realm;
    if (!realms.acceptInviteCode(code, realm, error) || realm.mId == 0) {
        ServiceErrorKind kind = realms.getLastError().mKind;
        error = kind == ServiceErrorKind::NotFound ? "This Realm invite link is not valid"
            : kind == ServiceErrorKind::Forbidden ? "You can't join this Realm with this invite link"
            : kind == ServiceErrorKind::Cancelled ? "Joining the Realm was cancelled"
            : "Could not join the Realm (" + std::string(ServiceError::name(kind)) + ")";
        return false;
    }
    realmId = realm.mId;
    return true;
}

bool parseHostPort(const std::string& address, std::string& host, unsigned short& port)
{
    size_t colon = address.rfind(':');
    host = colon == std::string::npos ? address : address.substr(0, colon);
    long parsed = colon == std::string::npos ? 19132 : std::strtol(address.c_str() + colon + 1, nullptr, 10);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    if (host.empty() || parsed <= 0 || parsed > 65535) {
        return false;
    }
    port = static_cast<unsigned short>(parsed);
    return true;
}

}

Session::Session()
    : publishedSnapshot(std::make_shared<const SessionSnapshot>())
{
}

Session::~Session()
{
    disconnect();
}

void Session::connect(std::string name, std::string target, MinecraftAuthentication* authentication, std::string offlineName)
{
    disconnect();
    cancelled = false;
    resetSnapshot(std::move(name), target);
    worker = std::thread([this, target = std::move(target), authentication, offlineName = std::move(offlineName)]() mutable {
        run(std::move(target), authentication, std::move(offlineName));
    });
}

void Session::resetSnapshot(std::string name, std::string target)
{
    std::lock_guard<std::mutex> guard(mutex);
    current = SessionSnapshot {};
    worldClock = {};
    current.state = resolvesOnline(target) ? SessionState::Resolving : SessionState::Connecting;
    current.name = std::move(name);
    current.target = std::move(target);
    ridingUnique = 0;
    pendingChat.clear();
    pendingActionbar.reset();
    pendingTitles.clear();
    pendingToasts.clear();
    outgoingChat.clear();
    pendingForms.clear();
    outgoingForms.clear();
    publishSnapshotLocked();
}

void Session::disconnect()
{
    cancelled = true;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (resourceReloadCancelled) resourceReloadCancelled->store(true, std::memory_order_relaxed);
    }
    if (worker.joinable()) {
        worker.join();
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state == SessionState::Joined) {
        current.state = SessionState::Idle;
    }
    publishSnapshotLocked();
}

std::string_view Session::gameVersion()
{
    return GameVersion;
}

int Session::protocolVersion()
{
    return ProtocolVersion;
}

SessionSnapshot Session::snapshot() const
{
    return *sharedSnapshot();
}

std::shared_ptr<const SessionSnapshot> Session::sharedSnapshot() const
{
    return std::atomic_load_explicit(&publishedSnapshot, std::memory_order_acquire);
}

void Session::publishSnapshotLocked(std::shared_ptr<const SessionSnapshot> snapshot)
{
    if (!snapshot) snapshot = std::make_shared<const SessionSnapshot>(current);
    auto previous = std::atomic_exchange_explicit(&publishedSnapshot, std::move(snapshot), std::memory_order_acq_rel);
    if (previous) retiredSnapshots.push_back(std::move(previous));
    if (snapshotProducerThread == std::thread::id {} || std::this_thread::get_id() == snapshotProducerThread) {
        retiredSnapshots.erase(std::remove_if(retiredSnapshots.begin(), retiredSnapshots.end(),
            [](const auto& retired) { return retired.use_count() == 1; }), retiredSnapshots.end());
    }
}

std::shared_ptr<const world::PackFiles> Session::cachedPack(const std::string& path)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto found = packCache.find(path);
    return found == packCache.end() ? nullptr : found->second;
}

void Session::cachePackLocked(const std::string& path, std::shared_ptr<const world::PackFiles> pack)
{
    constexpr size_t ArchiveBudget = 512ull * 1024 * 1024;
    packCache[path] = std::move(pack);
    size_t bytes = 0;
    for (const auto& [key, cached] : packCache) bytes += cached->archiveBytes();
    for (auto it = packCache.begin(); bytes > ArchiveBudget && it != packCache.end();) {
        if (it->first == path) { ++it; continue; }
        bytes -= it->second->archiveBytes();
        it = packCache.erase(it);
    }
}


std::vector<MeshUpdate> Session::takeMeshUpdates(size_t maximum, const world::BlockAssets* expectedAssets, MeshUpdateKind kind)
{
    std::array<float, 3> direction;
    {
        std::unique_lock<std::mutex> viewGuard(viewInputMutex, std::try_to_lock);
        if (!viewGuard.owns_lock()) return {};
        direction = requestedLookDirection;
    }
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (!guard.owns_lock()) return {};
    if (expectedAssets && current.assets.get() != expectedAssets) return {};
    std::vector<MeshUpdate> updates;
    updates.reserve(std::min(maximum, pendingUpdates.size()));
    world::MeshViewPriority priority(current.player.current, direction, startingTerrain);
    while (!pendingUpdates.empty() && updates.size() < maximum) {
        auto best = pendingUpdates.end();
        world::MeshPriority bestPriority;
        for (auto candidate = pendingUpdates.begin(); candidate != pendingUpdates.end(); ++candidate) {
            if ((kind == MeshUpdateKind::Removal && candidate->mesh) || (kind == MeshUpdateKind::Terrain && !candidate->mesh)) continue;
            if (kind == MeshUpdateKind::Removal) { best = candidate; break; }
            auto candidatePriority = priority.rank(candidate->key.x, candidate->key.y, candidate->key.z, candidate->urgent, candidate->refresh);
            if (best == pendingUpdates.end() || candidatePriority < bestPriority) {
                best = candidate;
                bestPriority = candidatePriority;
            }
        }
        if (best == pendingUpdates.end()) break;
        updates.push_back(std::move(*best));
        pendingUpdates.erase(best);
    }
    return updates;
}

std::vector<std::shared_ptr<const Packet>> Session::takeCameraEvents()
{
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (!guard.owns_lock()) return {};
    std::vector<std::shared_ptr<const Packet>> events;
    events.swap(pendingCameraEvents);
    return events;
}

/**
 * Tells the server the local player has finished loading, once, after its
 * PlayerSpawn status; the connection hands over before that status arrives so
 * chunks can stream in while the server waits for it.
 */
void Session::initializeLocalPlayer(BedrockConnection& target, uint64_t runtimeId)
{
    if (spawnInitialized) {
        return;
    }
    ServerboundLoadingScreenPacket loading;
    loading.mType = ServerboundLoadingScreenPacket::Type::EndLoadingScreen;
    target.send(loading);
    SetLocalPlayerAsInitializedPacket initialized;
    initialized.mRuntimeActorId = runtimeId;
    target.send(initialized);
    target.flush();
    spawnInitialized = true;
    debugLog("sent SetLocalPlayerAsInitialized");
}

void Session::setRenderDistance(int chunks)
{
    requestedRadius = chunks;
}

void Session::answerResourcePacks(bool download)
{
    packDecision = static_cast<int>(download ? ResourcePackDecision::Download : ResourcePackDecision::Skip);
    std::lock_guard<std::mutex> guard(mutex);
    current.packPrompt = false;
    current.packDownloading = download;
    current.packReceived = 0;
    current.packTotal = current.packBytes;
    publishSnapshotLocked();
}

void Session::setLookRay(const std::array<double, 3>& origin, const std::array<float, 3>& direction)
{
    std::lock_guard<std::mutex> guard(viewInputMutex);
    requestedLookOrigin = origin;
    requestedLookDirection = direction;
}

void Session::configurePaletteResolver()
{
    world.setBlockPaletteResolver([blockAssets = assets, mapping = ids,
        resolved = std::unordered_map<uint32_t, std::optional<uint32_t>> {}](const Tag& state) mutable -> std::optional<uint32_t> {
        const Tag* name = state.get("name");
        const Tag* properties = state.get("states");
        if (!blockAssets || !name || name->getType() != Tag::Type::String
            || !properties || properties->getType() != Tag::Type::Compound) return std::nullopt;
        std::string blockName = name->asString();
        if (blockName.find(':') == std::string::npos) {
            blockName = "minecraft:" + blockName;
        }
        uint32_t hash = static_cast<uint32_t>(BlockStateHasher::hash(blockName, *properties));
        auto found = resolved.find(hash);
        if (found != resolved.end()) return found->second;
        auto value = blockAssets->networkValueForState(hash, mapping.hashed, mapping.sequential.get());
        if (!value) {
            const Tag* version = state.get("version");
            int32_t stateVersion = version && version->getType() == Tag::Type::Int ? version->asInt() : 0;
            BlockStateData upgraded = blockStateUpgrader().upgrade(BlockStateData(blockName, *properties, stateVersion));
            uint32_t upgradedHash = static_cast<uint32_t>(BlockStateHasher::hash(upgraded.getName(), upgraded.getStates()));
            value = blockAssets->networkValueForState(upgradedHash, mapping.hashed, mapping.sequential.get());
        }
        resolved.emplace(hash, value);
        return value;
    });
}

/** The camera medium: 0 for air, 1 for water, 2 for lava, 3 for powder snow. */
uint8_t Session::mediumAt(const std::array<double, 3>& position)
{
    if (!assets) {
        return 0;
    }
    auto liquidAt = [&](int64_t x, int64_t y, int64_t z, uint8_t& level) -> uint8_t {
        world::SubChunkKey key { current.dimension, int32_t(x >> 4), int32_t(y >> 4), int32_t(z >> 4) };
        std::shared_ptr<const world::SubChunk> sub = world.store().subChunk(key);
        if (!sub) {
            return 0;
        }
        for (uint32_t layer = 0; layer < 2; ++layer) {
            uint32_t value = sub->runtimeId(layer, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
            if (value == world::ImplicitAir) {
                continue;
            }
            const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
            if (visual.powderSnow) return 3;
            if (visual.liquid) {
                level = visual.liquidLevel;
                return visual.liquid;
            }
        }
        return 0;
    };
    int64_t x = static_cast<int64_t>(std::floor(position[0]));
    int64_t y = static_cast<int64_t>(std::floor(position[1]));
    int64_t z = static_cast<int64_t>(std::floor(position[2]));
    uint8_t level = 0;
    uint8_t kind = liquidAt(x, y, z, level);
    if (kind == 3) return kind;
    if (!kind) {
        return 0;
    }
    uint8_t above = 0;
    double surface = level >= 8 || liquidAt(x, y + 1, z, above) == kind ? 1.0 : (8.0 - (level & 7)) / 9.0;
    return position[1] - static_cast<double>(y) < surface ? kind : 0;
}

void Session::setCameraBoom(const std::array<double, 3>& origin, const std::array<double, 3>& delta)
{
    std::lock_guard<std::mutex> guard(viewInputMutex);
    requestedBoomOrigin = origin;
    requestedBoomDelta = delta;
}

void Session::collectViewInput()
{
    std::lock_guard<std::mutex> guard(viewInputMutex);
    lookOrigin = requestedLookOrigin;
    renderedCamera = requestedRenderedCamera;
    lookDirection = requestedLookDirection;
    boomOrigin = requestedBoomOrigin;
    boomDelta = requestedBoomDelta;
    serverBoomOrigin = requestedServerBoomOrigin;
    serverBoomDelta = requestedServerBoomDelta;
}

void Session::setRenderedCamera(const std::array<double, 3>& origin)
{
    for (double axis : origin) if (!std::isfinite(axis) || std::abs(axis) > 30000000.0) return;
    std::lock_guard<std::mutex> guard(viewInputMutex);
    requestedRenderedCamera = origin;
}

std::pair<uint8_t, uint32_t> Session::cameraEnvironment(const SessionSnapshot& snapshot, const std::array<double, 3>& position, bool renderedSurface)
{
    if (!snapshot.assets || (!snapshot.nearby && !snapshot.cameraBlocks) || snapshot.state != SessionState::Joined) return { 0, 1 };
    for (double axis : position) if (!std::isfinite(axis) || std::abs(axis) > 30000000.0) return { 0, 1 };
    int32_t x = int32_t(std::floor(position[0])), y = int32_t(std::floor(position[1])), z = int32_t(std::floor(position[2]));
    world::SubChunkKey key { snapshot.dimension, x >> 4, y >> 4, z >> 4 };
    const auto& cameraBlocks = snapshot.cameraBlocks ? snapshot.cameraBlocks : snapshot.nearby;
    auto& area = *cameraBlocks;
    int32_t bx = key.x - area.base[0], by = key.y - area.base[1], bz = key.z - area.base[2];
    std::shared_ptr<const world::PalettedStorage> biome;
    if (area.dimension == snapshot.dimension && bx >= 0 && by >= 0 && bz >= 0
        && bx < NearbyBlocks::Span && by < NearbyBlocks::Span && bz < NearbyBlocks::Span) {
        size_t index = size_t((bx * NearbyBlocks::Span + by) * NearbyBlocks::Span + bz);
        if (index < area.biomes.size()) biome = area.biomes[index];
    }
    uint32_t biomeId = biome ? biome->runtimeId(x & 15, y & 15, z & 15) : (snapshot.dimension == 1 ? 8 : 1);
    auto visualAt = [&](int32_t atX, int32_t atY, int32_t atZ, uint32_t layer) -> const world::BlockVisual* {
        std::shared_ptr<const world::SubChunk> sub;
        auto& nearby = *cameraBlocks;
        int32_t sx = (atX >> 4) - nearby.base[0], sy = (atY >> 4) - nearby.base[1], sz = (atZ >> 4) - nearby.base[2];
        if (nearby.dimension == snapshot.dimension && sx >= 0 && sy >= 0 && sz >= 0
            && sx < NearbyBlocks::Span && sy < NearbyBlocks::Span && sz < NearbyBlocks::Span) {
            size_t index = size_t((sx * NearbyBlocks::Span + sy) * NearbyBlocks::Span + sz);
            if (index < nearby.subChunks.size()) sub = nearby.subChunks[index];
        }
        if (!sub && snapshot.loaded) {
            auto found = snapshot.loaded->subChunks.find({ snapshot.dimension, atX >> 4, atY >> 4, atZ >> 4 });
            if (found != snapshot.loaded->subChunks.end()) sub = found->second;
        }
        if (!sub) return nullptr;
        uint32_t value = sub->runtimeId(layer, atX & 15, atY & 15, atZ & 15);
        return value == world::ImplicitAir ? nullptr : &snapshot.assets->visual(value, area.ids.hashed, area.ids.sequential.get());
    };
    auto liquid = [&](int32_t atX, int32_t atY, int32_t atZ, uint8_t& level) -> uint8_t {
        for (uint32_t layer = 0; layer < 2; ++layer) {
            const auto* visual = visualAt(atX, atY, atZ, layer);
            if (visual && visual->liquid) { level = visual->liquidLevel; return visual->liquid; }
        }
        return 0;
    };
    if (const auto* visual = visualAt(x, y, z, 0); visual && visual->powderSnow) return { 3, biomeId };
    uint8_t level = 0, above = 0;
    uint8_t kind = liquid(x, y, z, level);
    if (!kind) return { 0, biomeId };
    double surface = level >= 8 || liquid(x, y + 1, z, above) == kind ? 1.0 : (8.0 - (level & 7)) / 9.0;
    if (renderedSurface) {
        constexpr int32_t corners[4][4][2] = {
            { {0,0}, {-1,0}, {0,-1}, {-1,-1} }, { {0,0}, {1,0}, {0,-1}, {1,-1} },
            { {0,0}, {1,0}, {0,1}, {1,1} }, { {0,0}, {-1,0}, {0,1}, {-1,1} }
        };
        std::array<float, 4> heights {};
        for (size_t corner = 0; corner < 4; ++corner) {
            const auto& offsets = corners[corner];
            uint8_t depth = 0;
            bool diagonal = liquid(x + offsets[1][0], y, z + offsets[1][1], depth) == kind
                || liquid(x + offsets[2][0], y, z + offsets[2][1], depth) == kind;
            size_t count = diagonal ? 4 : 3;
            bool covered = false;
            for (size_t i = 0; i < count; ++i)
                covered |= liquid(x + offsets[i][0], y + 1, z + offsets[i][1], depth) == kind;
            if (covered) { heights[corner] = 255; continue; }
            uint32_t total = 0, weight = 0;
            for (size_t i = 0; i < count; ++i) {
                int32_t sx = x + offsets[i][0], sz = z + offsets[i][1];
                uint8_t sample = liquid(sx, y, sz, depth);
                if (sample == kind) {
                    uint32_t height = depth >= 8 ? 227 : ((8 - (depth & 7)) * 255 + 4) / 9;
                    uint32_t sampleWeight = height >= 204 ? 10 : 1;
                    total += height * sampleWeight;
                    weight += sampleWeight;
                } else if (!sample) {
                    const auto* visual = visualAt(sx, y, sz, 0);
                    if (!visual || !(visual->flags & world::FlagOccludesFullFace)) ++weight;
                }
            }
            heights[corner] = weight ? float((total + weight / 2) / weight) : 0.0f;
        }
        float fx = float(position[0] - x), fz = float(position[2] - z);
        surface = (fz <= fx ? heights[0] + fx * (heights[1] - heights[0]) + fz * (heights[2] - heights[1])
            : heights[0] + fx * (heights[2] - heights[3]) + fz * (heights[3] - heights[0])) / 255.0f;
    }
    return { kind && position[1] - y < surface ? kind : uint8_t(0), biomeId };
}

void Session::setServerCameraBoom(const std::array<double, 3>& origin, const std::array<double, 3>& delta)
{
    std::lock_guard<std::mutex> guard(viewInputMutex);
    requestedServerBoomOrigin = origin;
    requestedServerBoomDelta = delta;
}

/**
 * How far along the camera boom a box of radius 0.2 at the eye can travel
 * before touching a block, as a fraction of the boom, backed off by a
 * thousandth of a block. A block the eye already sits within 0.2 of only
 * stops the boom at its real faces, otherwise hugging a wall would pull the
 * camera into the head. Missing sub-chunks of a loaded column are air, since
 * servers skip empty ones; unloaded columns stop the camera at the eye.
 */
double Session::boomFraction()
{
    return boomFraction(boomOrigin, boomDelta);
}

double Session::boomFraction(const std::array<double, 3>& boomOrigin, const std::array<double, 3>& boomDelta)
{
    constexpr double Radius = 0.2;
    constexpr double Epsilon = 0.001;
    double length = std::sqrt(boomDelta[0] * boomDelta[0] + boomDelta[1] * boomDelta[1] + boomDelta[2] * boomDelta[2]);
    if (length < 1.0e-6) {
        return 1.0;
    }
    if (!std::isfinite(length) || length > 1024.0) return 0.0;
    for (double coordinate : boomOrigin) if (!std::isfinite(coordinate) || std::abs(coordinate) > 30000000.0) return 0.0;
    // Sample candidate cells along the swept camera box, then intersect their
    // exact collision boxes. Large orbital radii must not scan a cubic volume.
    std::set<std::array<int32_t, 3>> cells;
    size_t steps = static_cast<size_t>(std::ceil(length * 2.0));
    for (size_t step = 0; step <= steps; ++step) {
        std::array<int32_t, 3> cell {};
        for (size_t axis = 0; axis < 3; ++axis)
            cell[axis] = static_cast<int32_t>(std::floor(boomOrigin[axis] + boomDelta[axis] * double(step) / double(steps)));
        for (int32_t x = -1; x <= 1; ++x)
            for (int32_t y = -1; y <= 1; ++y)
                for (int32_t z = -1; z <= 1; ++z) cells.insert({ cell[0] + x, cell[1] + y, cell[2] + z });
    }
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    world::BlockCollisions::Lookup lookup = [this](int32_t x, int32_t y, int32_t z) {
        return motionCell(x, y, z).primary;
    };
    std::vector<world::CollisionBox> boxes;
    for (const auto& cell : cells) {
        auto [x, y, z] = cell;
        if (!world.store().isLoaded({ motionDimension, x >> 4, z >> 4 })) return 0.0;
        if (const world::CollisionState* state = motionCell(x, y, z).primary) {
            table.boxes(*state, x, y, z, lookup, boxes);
        }
    }
    double fraction = 1.0;
    for (const world::CollisionBox& box : boxes) {
        auto encloses = [&](double margin) {
            return boomOrigin[0] > box.minX - margin && boomOrigin[0] < box.maxX + margin
                && boomOrigin[1] > box.minY - margin && boomOrigin[1] < box.maxY + margin
                && boomOrigin[2] > box.minZ - margin && boomOrigin[2] < box.maxZ + margin;
        };
        if (encloses(0.0)) {
            continue;
        }
        double margin = encloses(Radius) ? 0.0 : Radius;
        std::array<double, 3> minimum { box.minX - margin, box.minY - margin, box.minZ - margin };
        std::array<double, 3> maximum { box.maxX + margin, box.maxY + margin, box.maxZ + margin };
        double entry = 0.0;
        double exit = 1.0;
        bool missed = false;
        for (size_t axis = 0; axis < 3 && !missed; ++axis) {
            if (std::abs(boomDelta[axis]) <= 1.0e-12) {
                missed = boomOrigin[axis] < minimum[axis] || boomOrigin[axis] > maximum[axis];
                continue;
            }
            double first = (minimum[axis] - boomOrigin[axis]) / boomDelta[axis];
            double second = (maximum[axis] - boomOrigin[axis]) / boomDelta[axis];
            entry = std::max(entry, std::min(first, second));
            exit = std::min(exit, std::max(first, second));
        }
        if (!missed && entry <= exit) {
            fraction = std::min(fraction, entry);
        }
    }
    if (fraction >= 1.0) {
        return 1.0;
    }
    return std::max(fraction * length - Epsilon, 0.0) / length;
}

std::optional<TargetBlock> Session::traceTarget()
{
    std::optional<BlockHit> hit = traceBlock(20.0);
    if (!hit) {
        return std::nullopt;
    }
    TargetBlock target;
    target.cell = hit->cell;
    target.name = assets->blockName(hit->value, ids.hashed, ids.sequential.get());
    if (target.name.empty()) {
        target.name = hit->name;
    }
    const Tag* states = assets->blockStates(hit->value, ids.hashed, ids.sequential.get());
    if (!states || !states->isCompound()) {
        return target;
    }
    const std::vector<std::string>& keys = states->getKeys();
    const std::vector<Tag>& values = states->getValues();
    for (size_t i = 0; i < keys.size() && i < values.size(); ++i) {
        const Tag& value = values[i];
        std::string shown;
        switch (value.getType()) {
        case Tag::Type::Byte:
            shown = value.asByte() ? "\xC2\xA7" "atrue" : "\xC2\xA7" "cfalse";
            break;
        case Tag::Type::Short:
            shown = std::to_string(value.asShort());
            break;
        case Tag::Type::Int:
            shown = std::to_string(value.asInt());
            break;
        case Tag::Type::String:
            shown = value.asString();
            break;
        default:
            continue;
        }
        target.states.push_back(keys[i] + ": " + shown);
    }
    return target;
}

void Session::handleViolation(const PacketViolationWarningPacket& violation)
{
    auto cause = static_cast<MinecraftPacketIds>(violation.mPacketCauseId);
    std::string severity = "unknown";
    switch (violation.mSeverity) {
    case PacketViolationSeverity::Warning:
        severity = "warning";
        break;
    case PacketViolationSeverity::FinalWarning:
        severity = "final warning";
        break;
    case PacketViolationSeverity::TerminatingConnection:
        severity = "terminating connection";
        break;
    default:
        break;
    }
    std::string details = "Packet violation (" + severity + ")"
        + "\nType: " + (violation.mType == PacketViolationType::MalformedPacket ? "malformed packet" : "unknown")
        + "\nPacket: " + toString(cause) + " (" + std::to_string(violation.mPacketCauseId) + ")";
    if (!violation.mContext.empty()) {
        details += "\nContext: " + violation.mContext;
    }
    debugLog(details);
    std::lock_guard<std::mutex> guard(mutex);
    current.packetError = std::move(details);
}

void Session::handleWorldPacket(std::string& payload)
{
    MinecraftPacketIds id;
    if (!BedrockConnection::peekPacketId(payload, id)) {
        return;
    }
    journal.record(false, static_cast<int>(id), payload);
    if (packetHook) {
        if (!packetHook->inbound(static_cast<int>(id), payload) || !BedrockConnection::peekPacketId(payload, id)) {
            return;
        }
    }
    if (seenPackets.insert(static_cast<int>(id)).second) {
        debugLog("first world packet " + std::to_string(static_cast<int>(id)));
    }

    switch (id) {
    case MinecraftPacketIds::CameraInstruction:
    case MinecraftPacketIds::CameraPresets:
    case MinecraftPacketIds::CameraShake:
    case MinecraftPacketIds::PlayStatus:
    case MinecraftPacketIds::BlockActorData:
    case MinecraftPacketIds::LevelChunk:
    case MinecraftPacketIds::SubChunk:
    case MinecraftPacketIds::UpdateBlock:
    case MinecraftPacketIds::UpdateBlockSynced:
    case MinecraftPacketIds::UpdateSubChunkBlocks:
    case MinecraftPacketIds::NetworkChunkPublisherUpdate:
    case MinecraftPacketIds::ChunkRadiusUpdated:
    case MinecraftPacketIds::DimensionData:
    case MinecraftPacketIds::BlockEvent:
    case MinecraftPacketIds::ChangeDimension:
    case MinecraftPacketIds::MovePlayer:
    case MinecraftPacketIds::AddPlayer:
    case MinecraftPacketIds::AddItemActor:
    case MinecraftPacketIds::TakeItemActor:
    case MinecraftPacketIds::AddActor:
    case MinecraftPacketIds::Animate:
    case MinecraftPacketIds::RemoveActor:
    case MinecraftPacketIds::MoveActorAbsolute:
    case MinecraftPacketIds::MoveActorDelta:
    case MinecraftPacketIds::SetActorData:
    case MinecraftPacketIds::SetActorLink:
    case MinecraftPacketIds::ContainerOpen:
    case MinecraftPacketIds::ContainerClose:
    case MinecraftPacketIds::ContainerSetData:
    case MinecraftPacketIds::PlayerEnchantOptions:
    case MinecraftPacketIds::TrimData:
    case MinecraftPacketIds::NpcDialogue:
    case MinecraftPacketIds::ItemStackResponse:
    case MinecraftPacketIds::CreativeContent:
    case MinecraftPacketIds::CraftingData:
    case MinecraftPacketIds::InventoryContent:
    case MinecraftPacketIds::InventorySlot:
    case MinecraftPacketIds::MobEquipment:
    case MinecraftPacketIds::MobArmorEquipment:
    case MinecraftPacketIds::PlayerHotbar:
    case MinecraftPacketIds::UpdateAttributes:
    case MinecraftPacketIds::SetHealth:
    case MinecraftPacketIds::ActorEvent:
    case MinecraftPacketIds::SetPlayerGameType:
    case MinecraftPacketIds::UpdatePlayerGameType:
    case MinecraftPacketIds::MobEffect:
    case MinecraftPacketIds::BossEvent:
    case MinecraftPacketIds::PlayerList:
    case MinecraftPacketIds::PlayerSkin:
    case MinecraftPacketIds::SetTime:
    case MinecraftPacketIds::PlayerFog:
    case MinecraftPacketIds::SyncWorldClocks:
    case MinecraftPacketIds::GameRulesChanged:
    case MinecraftPacketIds::LevelEvent:
    case MinecraftPacketIds::NetworkStackLatency:
    case MinecraftPacketIds::Respawn:
    case MinecraftPacketIds::PlayerAction:
    case MinecraftPacketIds::DeathInfo:
    case MinecraftPacketIds::LevelSoundEvent:
    case MinecraftPacketIds::PlaySound:
    case MinecraftPacketIds::StopSound:
    case MinecraftPacketIds::CorrectPlayerMovePrediction:
    case MinecraftPacketIds::SetActorMotion:
    case MinecraftPacketIds::UpdateAbilities:
    case MinecraftPacketIds::UpdateClientInputLocks:
    case MinecraftPacketIds::Text:
    case MinecraftPacketIds::SetTitle:
    case MinecraftPacketIds::ToastRequest:
    case MinecraftPacketIds::AvailableCommands:
    case MinecraftPacketIds::CommandOutput:
    case MinecraftPacketIds::SetDisplayObjective:
    case MinecraftPacketIds::SetScore:
    case MinecraftPacketIds::RemoveObjective:
    case MinecraftPacketIds::ModalFormRequest:
    case MinecraftPacketIds::ClientboundCloseForm:
    case MinecraftPacketIds::PacketViolationWarning:
    case MinecraftPacketIds::Transfer:
    case MinecraftPacketIds::SetHud:
    case MinecraftPacketIds::SpawnParticleEffect:
    case MinecraftPacketIds::ClientboundMapItemData:
    case MinecraftPacketIds::UpdateTrade:
        break;
    default:
        return;
    }

    const size_t payloadSize = payload.size();
    std::shared_ptr<Packet> packet = connection->decode(std::move(payload));
    if (!packet) {
        std::string details = std::string("Could not read ") + toString(id) + " (" + std::to_string(static_cast<int>(id)) + ")"
            + "\nSize: " + std::to_string(payloadSize) + " bytes"
            + "\nError: " + connection->getLastDecodeError();
        debugLog(details);
        {
            std::lock_guard<std::mutex> guard(mutex);
            current.packetError = std::move(details);
        }
        connection->disconnect("Bad packet received from server");
        return;
    }
    std::optional<GameType> gameType;
    if (auto mode = std::dynamic_pointer_cast<SetPlayerGameTypePacket>(packet)) {
        gameType = static_cast<GameType>(mode->mGamemode);
    } else if (auto mode = std::dynamic_pointer_cast<UpdatePlayerGameTypePacket>(packet); mode && mode->mActorId == localUniqueId) {
        gameType = mode->mGameType;
    }
    if (gameType) {
        motion.setGameType(static_cast<int32_t>(*gameType));
        std::lock_guard<std::mutex> guard(mutex);
        current.hud.gameType = static_cast<int32_t>(*gameType);
        current.gameMode = gameModeName(*gameType);
    }
    handleInventoryPacket(packet);
    handleHudPacket(packet);
    handleMotionPacket(packet);
    handleSoundPacket(packet);
    handleChatPacket(packet);
    handleScorePacket(packet);
    handleFormPacket(packet);

    if (auto violation = std::dynamic_pointer_cast<PacketViolationWarningPacket>(packet)) {
        handleViolation(*violation);
        return;
    }

    if (auto transfer = std::dynamic_pointer_cast<TransferPacket>(packet)) {
        // A slash would turn the address into a Realm or experience target.
        if (transfer->mAddress.empty() || transfer->mAddress.find('/') != std::string::npos || transfer->mPort <= 0 || transfer->mPort > 65535) {
            debugLog("ignored transfer to " + transfer->mAddress + ":" + std::to_string(transfer->mPort));
            return;
        }
        std::string host = transfer->mAddress.find(':') != std::string::npos ? "[" + transfer->mAddress + "]" : transfer->mAddress;
        transferTarget = host + ":" + std::to_string(transfer->mPort);
        return;
    }

    if (auto event = std::dynamic_pointer_cast<ActorEventPacket>(packet);
        event) {
        double now = secondsNow();
        if (auto actor = actors.find(event->mRuntimeActorId); actor != actors.end()) {
            if (event->mEventId == static_cast<uint8_t>(EntityEventType::HurtAnimation)) actor->second.lastHurt = now;
            if (event->mEventId == static_cast<uint8_t>(EntityEventType::DeathAnimation)
                || event->mEventId == static_cast<uint8_t>(EntityEventType::EnderDragonDeath)) actor->second.diedAt = now;
            if (event->mEventId == static_cast<uint8_t>(EntityEventType::Respawn)) actor->second.diedAt = 0.0;
        }
    }
    if (auto attributes = std::dynamic_pointer_cast<UpdateAttributesPacket>(packet)) {
        if (auto actor = actors.find(static_cast<uint64_t>(attributes->mRuntimeActorId)); actor != actors.end()) {
            for (const auto& attribute : attributes->mAttributes) {
                if (attribute.mName != "minecraft:health" || !std::isfinite(attribute.mValue)) continue;
                actor->second.health = attribute.mValue;
                if (std::isfinite(attribute.mMaximum) && attribute.mMaximum > 0.0f) actor->second.maxHealth = attribute.mMaximum;
                if (attribute.mValue <= 0.0f && actor->second.diedAt == 0.0) actor->second.diedAt = secondsNow();
                if (attribute.mValue > 0.0f) actor->second.diedAt = 0.0;
            }
        }
    }

    if (auto animation = std::dynamic_pointer_cast<AnimatePacket>(packet); animation && animation->mAction == AnimatePacket::Action::SwingArm) {
        if (auto actor = actors.find(animation->mRuntimeActorId); actor != actors.end()) {
            actor->second.lastSwing = secondsNow();
        }
    }

    if (auto equipment = std::dynamic_pointer_cast<MobArmorEquipmentPacket>(packet)) {
        if (auto actor = actors.find(static_cast<uint64_t>(equipment->mRuntimeActorId)); actor != actors.end()) {
            actor->second.armorItems = { hudItemOf(equipment->mHelmet), hudItemOf(equipment->mChestplate), hudItemOf(equipment->mLeggings), hudItemOf(equipment->mBoots) };
            actor->second.armor = {
                actor->second.armorItems[0].identifier,
                actor->second.armorItems[1].identifier,
                actor->second.armorItems[2].identifier,
                actor->second.armorItems[3].identifier,
            };
        }
    }

    if (auto equipment = std::dynamic_pointer_cast<MobEquipmentPacket>(packet); equipment && (equipment->mContainerId == 0 || equipment->mContainerId == 119)) {
        if (auto actor = actors.find(static_cast<uint64_t>(equipment->mRuntimeActorId)); actor != actors.end()) {
            const bool offhand = actorEquipmentIsOffhand(actor->second.identifier, equipment->mContainerId, equipment->mInventorySlot);
            (offhand ? actor->second.offhand : actor->second.held) = hudItemOf(equipment->mItem);
        }
    }

    auto syncFallingBlock = [this](uint64_t uniqueId, bool landed) {
        if (auto runtime = runtimeByUnique.find(static_cast<int64_t>(uniqueId)); runtime != runtimeByUnique.end()) {
            if (auto actor = actors.find(runtime->second); actor != actors.end() && actor->second.identifier == "minecraft:falling_block")
                actor->second.fallingBlockLanded = landed;
        }
    };
    if (auto levelChunk = std::dynamic_pointer_cast<LevelChunkPacket>(packet)) {
        if (world.stats().levelChunks < 8) {
            debugLog("LevelChunk received x=" + std::to_string(levelChunk->mChunkX) + " z=" + std::to_string(levelChunk->mChunkZ)
                     + " dimension=" + std::to_string(levelChunk->mDimension) + " subchunks=" + std::to_string(levelChunk->mSubChunksLength)
                     + " request-subchunks=" + (levelChunk->mRequestSubChunks ? "yes" : "no")
                     + " cached=" + (levelChunk->mCachingEnabled ? "yes" : "no") + " bytes=" + std::to_string(levelChunk->mData.size()));
        }
        world.handle(levelChunk);
    } else if (auto subChunk = std::dynamic_pointer_cast<SubChunkPacket>(packet)) {
        world.handle(subChunk);
    } else if (auto updateBlock = std::dynamic_pointer_cast<UpdateBlockPacket>(packet)) {
        if (updateBlock->mDataLayer == 0) {
            const Vector3i& at = updateBlock->mBlockPosition;
            answerPredictedBreak({ at.x, at.y, at.z }, updateBlock->mRuntimeId);
        }
        world.handle(*updateBlock);
    } else if (auto synced = std::dynamic_pointer_cast<UpdateBlockSyncedPacket>(packet)) {
        if (synced->mDataLayer == 0) {
            const auto& at = synced->mBlockPosition;
            answerPredictedBreak({ at.x, at.y, at.z }, synced->mRuntimeId);
        }
        world.handle(*synced);
        if (synced->mDataLayer == 0 && (synced->mEntityBlockSyncType == BlockSyncType::Create || synced->mEntityBlockSyncType == BlockSyncType::Destroy))
            syncFallingBlock(synced->mRuntimeActorId, synced->mEntityBlockSyncType == BlockSyncType::Destroy);
    } else if (auto updateSubChunk = std::dynamic_pointer_cast<UpdateSubChunkBlocksPacket>(packet)) {
        for (const BlockChangeEntry& entry : updateSubChunk->mStandardBlocks) {
            answerPredictedBreak({ entry.mPosition.x, entry.mPosition.y, entry.mPosition.z }, entry.mRuntimeId);
            if (entry.mMessageType == BlockChangeMessageType::Create || entry.mMessageType == BlockChangeMessageType::Destroy)
                syncFallingBlock(entry.mMessageEntityId, entry.mMessageType == BlockChangeMessageType::Destroy);
        }
        world.handle(updateSubChunk);
    } else if (auto publisher = std::dynamic_pointer_cast<NetworkChunkPublisherUpdatePacket>(packet)) {
        world.handle(*publisher);
    } else if (auto dimensions = std::dynamic_pointer_cast<DimensionDataPacket>(packet)) {
        static const std::pair<const char*, int32_t> Names[] = {
            { "minecraft:overworld", 0 }, { "minecraft:nether", 1 }, { "minecraft:the_end", 2 },
        };
        for (const DimensionDefinition& definition : dimensions->mDefinitions) {
            for (const auto& [name, dimension] : Names) {
                if (definition.mId == name) {
                    world::setServerDimensionHeight(dimension, definition.mMinimumHeight, definition.mMaximumHeight);                }
            }
        }
    } else if (auto radius = std::dynamic_pointer_cast<ChunkRadiusUpdatedPacket>(packet)) {
        debugLog("server chunk radius " + std::to_string(radius->mRadius));
        world.setChunkRadius(radius->mRadius);
        std::lock_guard<std::mutex> guard(mutex);
        current.chunkRadius = radius->mRadius;
    } else if (auto move = std::dynamic_pointer_cast<MovePlayerPacket>(packet)) {
        if (static_cast<uint64_t>(move->mRuntimeActorId) == localRuntimeId) {
            std::lock_guard<std::mutex> guard(mutex);
            current.spawnX = move->mPosition.x;
            current.spawnY = move->mPosition.y;
            current.spawnZ = move->mPosition.z;
            current.spawnPitch = move->mRotation.x;
            current.spawnYaw = move->mRotation.y;
            ++current.teleportCount;
        } else {
            bool teleport = move->mMode == MovePlayerMode::Teleport || move->mMode == MovePlayerMode::Respawn;
            moveActor(static_cast<uint64_t>(move->mRuntimeActorId), move->mPosition.x, move->mPosition.y, move->mPosition.z, move->mRotation.y, move->mRotation.z, move->mRotation.x, teleport, move->mOnGround);
        }
    } else if (auto player = std::dynamic_pointer_cast<AddPlayerPacket>(packet)) {
        uint64_t runtime = static_cast<uint64_t>(player->mRuntimeActorId);
        if (runtime != localRuntimeId) {
            ActorView actor;
            actor.runtimeId = runtime;
            actor.uniqueId = player->mRuntimeActorId;
            actor.identifier = "minecraft:player";
            actor.name = player->mUsername;
            std::string uuid = player->mUuid.toString();
            uuidByRuntime[runtime] = uuid;
            if (!skinByUuid.contains(uuid)) {
                assignSkin(uuid);
            }
            if (auto skin = skinByUuid.find(uuid); skin != skinByUuid.end()) {
                actor.skinSlot = skin->second.first;
                actor.slim = skin->second.second;
            }
            actor.held = hudItemOf(player->mHand);
            actor.scale = metadataScale(player->mMetadata, 1.0f);
            applyActorMetadata(player->mMetadata, actor);
            actors[runtime] = actor;
            runtimeByUnique[player->mRuntimeActorId] = runtime;
            moveActor(runtime, player->mPosition.x, player->mPosition.y, player->mPosition.z, player->mRotation.y, player->mRotation.z, player->mRotation.x, true, true, true);
        }
    } else if (auto added = std::dynamic_pointer_cast<AddActorPacket>(packet)) {
        uint64_t runtime = static_cast<uint64_t>(added->mRuntimeActorId);
        ActorView actor;
        actor.runtimeId = runtime;
        actor.uniqueId = added->mUniqueActorId;
        actor.identifier = added->mIdentifier;
        actor.scale = metadataScale(added->mMetadata, 1.0f);
        applyActorMetadata(added->mMetadata, actor);
        for (const auto& attribute : added->mAttributes) {
            if (attribute.mName == "minecraft:health" && std::isfinite(attribute.mValue)) {
                actor.health = attribute.mValue;
                if (std::isfinite(attribute.mMaximum) && attribute.mMaximum > 0.0f) actor.maxHealth = attribute.mMaximum;
            }
        }
        actors[runtime] = actor;
        runtimeByUnique[added->mUniqueActorId] = runtime;
        moveActor(runtime, added->mPosition.x, added->mPosition.y, added->mPosition.z, added->mBodyRotation, added->mHeadRotation, added->mRotation.x, true, true, true);
    } else if (auto dropped = std::dynamic_pointer_cast<AddItemActorPacket>(packet)) {
        uint64_t runtime = dropped->mRuntimeActorId;
        ActorView actor;
        actor.runtimeId = runtime;
        actor.uniqueId = dropped->mUniqueActorId;
        actor.identifier = "minecraft:item";
        actor.item = hudItemOf(dropped->mItemInHand);
        actor.width = 0.25f;
        actor.height = 0.25f;
        applyActorMetadata(dropped->mMetadata, actor);
        actors[runtime] = actor;
        runtimeByUnique[dropped->mUniqueActorId] = runtime;
        moveActor(runtime, dropped->mPosition.x, dropped->mPosition.y, dropped->mPosition.z, 0.0f, 0.0f, 0.0f, true, false, true);
    } else if (auto taken = std::dynamic_pointer_cast<TakeItemActorPacket>(packet)) {
        if (auto item = actors.find(taken->mItemRuntimeActorId); item != actors.end() && item->second.pickedUpAt == 0.0) {
            item->second.pickedUpBy = taken->mRuntimeActorId;
            item->second.pickedUpAt = secondsNow();
        }
    } else if (auto data = std::dynamic_pointer_cast<SetActorDataPacket>(packet)) {
        if (static_cast<uint64_t>(data->mRuntimeActorId) == localRuntimeId) {
            for (const EntityDataEntry& entry : data->mMetadata.mEntries) {
                if (entry.mFormat != EntityDataFormat::Long) continue;
                if (entry.mId == 0) current.localActorFlags[0] = static_cast<uint64_t>(entry.mLongValue);
                if (entry.mId == 92) current.localActorFlags[1] = static_cast<uint64_t>(entry.mLongValue);
            }
        }
        if (auto actor = actors.find(static_cast<uint64_t>(data->mRuntimeActorId)); actor != actors.end()) {
            actor->second.scale = metadataScale(data->mMetadata, actor->second.scale);
            applyActorMetadata(data->mMetadata, actor->second);
        }
    } else if (auto link = std::dynamic_pointer_cast<SetActorLinkPacket>(packet)) {
        const EntityLinkData& data = link->mActorLink;
        if (data.mTo == localUniqueId && data.mType != EntityLinkType::Remove) {
            ridingUnique = data.mFrom;
            std::string identifier;
            if (auto runtime = runtimeByUnique.find(data.mFrom); runtime != runtimeByUnique.end()) {
                if (auto actor = actors.find(runtime->second); actor != actors.end()) {
                    identifier = actor->second.identifier;
                }
            }
            std::lock_guard<std::mutex> guard(mutex);
            current.riding = identifier.empty() ? "minecraft:unknown" : identifier;
        } else if (data.mTo == localUniqueId) {
            ridingUnique = 0;
            std::lock_guard<std::mutex> guard(mutex);
            current.riding.clear();
        }
    } else if (auto removed = std::dynamic_pointer_cast<RemoveActorPacket>(packet)) {
        if (ridingUnique != 0 && removed->mUniqueActorId == ridingUnique) {
            ridingUnique = 0;
            std::lock_guard<std::mutex> guard(mutex);
            current.riding.clear();
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            std::erase_if(current.hud.bossBars, [&](const BossBarView& bar) {
                return bar.bossId == removed->mUniqueActorId;
            });
        }
        auto runtime = runtimeByUnique.find(removed->mUniqueActorId);
        if (auto picked = runtime != runtimeByUnique.end() ? actors.find(runtime->second) : actors.end(); picked != actors.end() && picked->second.pickedUpAt > 0.0) {
            runtimeByUnique.erase(runtime);
        } else if (runtime != runtimeByUnique.end()) {
            actors.erase(runtime->second);
            if (auto owner = uuidByRuntime.find(runtime->second); owner != uuidByRuntime.end()) {
                std::string uuid = owner->second;
                uuidByRuntime.erase(owner);
                releaseSkin(uuid);
            }
            runtimeByUnique.erase(runtime);
        }
    } else if (auto absolute = std::dynamic_pointer_cast<MoveActorAbsolutePacket>(packet)) {
        moveActor(static_cast<uint64_t>(absolute->mRuntimeActorId), absolute->mPosition.x, absolute->mPosition.y, absolute->mPosition.z, absolute->mRotation.y, absolute->mRotation.z, absolute->mRotation.x, absolute->mTeleported, absolute->mOnGround);
    } else if (auto delta = std::dynamic_pointer_cast<MoveActorDeltaPacket>(packet)) {
        auto actor = actors.find(delta->mRuntimeActorId);
        if (actor != actors.end()) {
            double eye = actor->second.identifier == "minecraft:player" ? PlayerEyeHeight : 0.0;
            if (actor->second.identifier == "minecraft:falling_block") eye = 0.49;
            moveActor(delta->mRuntimeActorId, delta->mHasX ? delta->mX : actor->second.x, delta->mHasY ? delta->mY : actor->second.y + eye,
                delta->mHasZ ? delta->mZ : actor->second.z, delta->mHasYaw ? delta->mYaw : actor->second.yaw, delta->mHasHeadYaw ? delta->mHeadYaw : actor->second.headYaw,
                delta->mHasPitch ? delta->mPitch : actor->second.pitch, false, delta->mOnGround);
        }
    } else if (auto changed = std::dynamic_pointer_cast<PlayerSkinPacket>(packet)) {
        std::string uuid = changed->mUuid.toString();
        storeSkin(uuid, changed->mSkin);
        if (uuid == localUuid) {
            if (auto skin = skinByUuid.find(uuid); skin != skinByUuid.end()) {
                std::lock_guard<std::mutex> guard(mutex);
                current.localSkinSlot = skin->second.first;
                current.localSlim = skin->second.second;
            }
        }
    } else if (auto list = std::dynamic_pointer_cast<PlayerListPacket>(packet)) {
        for (const PlayerListPacket::Entry& entry : list->mEntries) {
            std::string uuid = entry.mUuid.toString();
            if (entry.mAction == PlayerListPacket::Action::Remove) {
                playerNames.erase(uuid);
                releaseSkin(uuid);
                continue;
            }
            if (entry.mActorId == localUniqueId) {
                localUuid = uuid;
            }
            playerNames[uuid] = entry.mName;
            playerNamesByActor[entry.mActorId] = entry.mName;
            storeSkin(uuid, entry.mSkin);
        }
        if (auto skin = skinByUuid.find(localUuid); skin != skinByUuid.end()) {
            std::lock_guard<std::mutex> guard(mutex);
            current.localSkinSlot = skin->second.first;
            current.localSlim = skin->second.second;
        }
        std::vector<std::string> names;
        names.reserve(playerNames.size());
        for (const auto& [uuid, name] : playerNames) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        std::lock_guard<std::mutex> guard(mutex);
        current.players = std::move(names);
    } else if (auto fog = std::dynamic_pointer_cast<PlayerFogPacket>(packet)) {
        auto stack = std::make_shared<const std::vector<std::string>>(std::move(fog->mFogStack));
        std::lock_guard<std::mutex> guard(mutex);
        current.fogStack = std::move(stack);
    } else if (auto clocks = std::dynamic_pointer_cast<SyncWorldClocksPacket>(packet)) {
        if (auto state = worldClock.apply(*clocks)) {
            std::lock_guard<std::mutex> guard(mutex);
            current.worldTime = state->time;
            current.worldTimeStamp = secondsNow();
            current.worldClockPaused = state->paused;
        }
    } else if (auto time = std::dynamic_pointer_cast<SetTimePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        current.worldTime = time->mTime;
        current.worldClockPaused = false;
        current.worldTimeStamp = secondsNow();
        debugLog("set time " + std::to_string(time->mTime));
    } else if (auto rules = std::dynamic_pointer_cast<GameRulesChangedPacket>(packet)) {
        for (const ChangedGameRuleData& rule : rules->mGameRules) {
            if (rule.mType == ChangedGameRuleType::Bool && util::lowercase(rule.mName) == "dodaylightcycle") {
                std::lock_guard<std::mutex> guard(mutex);
                current.worldTime = currentWorldTime(current);
                current.worldTimeStamp = secondsNow();
                current.daylightCycle = rule.mBoolValue;
            } else if (rule.mType == ChangedGameRuleType::Bool && util::lowercase(rule.mName) == "showcoordinates") {
                std::lock_guard<std::mutex> guard(mutex);
                current.showCoordinates = rule.mBoolValue;
            }
        }
    } else if (auto actor = std::dynamic_pointer_cast<BlockActorDataPacket>(packet)) {
        world.handle(actor, payloadSize);
        std::array<int32_t, 3> cell { actor->mBlockPosition.x, actor->mBlockPosition.y, actor->mBlockPosition.z };
        if (chestLidStates.contains(cell)) {
            markChestLid(cell, true);
        }
    } else if (auto blockEvent = std::dynamic_pointer_cast<BlockEventPacket>(packet)) {
        constexpr int32_t ChestEvent = 1;
        if (blockEvent->mEventType == ChestEvent) {
            std::array<int32_t, 3> cell { blockEvent->mBlockPosition.x, blockEvent->mBlockPosition.y, blockEvent->mBlockPosition.z };
            chestLidStates[cell].open = blockEvent->mEventData > 0;
        }
    } else if (auto status = std::dynamic_pointer_cast<PlayStatusPacket>(packet)) {
        debugLog("play status " + std::to_string(static_cast<int>(status->mStatus)));
        if (status->mStatus == PlayStatusPacket::Status::PlayerSpawn) {
            initializeLocalPlayer(*connection, localRuntimeId);
            dimensionSpawnReceived = true;
        }
    } else if (auto map = std::dynamic_pointer_cast<ClientboundMapItemDataPacket>(packet)) {
        handleMapPacket(*map);
    } else if (auto effect = std::dynamic_pointer_cast<SpawnParticleEffectPacket>(packet)) {
        if (effect->mDimensionId != motionDimension || effect->mIdentifier.empty()) {
            return;
        }
        world::ParticleSpawn spawn = particleForSpawnPacket(effect->mIdentifier, { effect->mPosition.x, effect->mPosition.y, effect->mPosition.z },
            effect->mHasMolangVariablesJson ? effect->mMolangVariablesJson : std::string());
        if (effect->mUniqueActorId != -1) {
            if (auto runtime = runtimeByUnique.find(effect->mUniqueActorId); runtime != runtimeByUnique.end()) {
                spawn.attachedActor = runtime->second;
            } else if (effect->mUniqueActorId == localUniqueId) {
                spawn.attachedActor = localRuntimeId;
            }
        }
        queueParticle(std::move(spawn));
    } else if (auto event = std::dynamic_pointer_cast<LevelEventPacket>(packet)) {
        handleBreakingEvent(*event);
        int32_t eventId = event->mEventId;
        bool breaking = eventId == LevelEventPacket::ParticleDestroy || eventId == LevelEventPacket::ParticlePunchBlock || (eventId >= 3600 && eventId <= 3608);
        if (!breaking) {
            if (std::optional<world::ParticleSpawn> spawn = particleForLevelEvent(eventId, { event->mPosition.x, event->mPosition.y, event->mPosition.z }, event->mData)) {
                queueParticle(std::move(*spawn));
            }
        }
        std::lock_guard<std::mutex> guard(mutex);
        switch (event->mEventId) {
        case LevelEventPacket::StartRain:
            current.rainLevel = 1.0f;
            break;
        case LevelEventPacket::StopRain:
            current.rainLevel = 0.0f;
            break;
        case LevelEventPacket::StartThunder:
            current.thunderLevel = 1.0f;
            break;
        case LevelEventPacket::StopThunder:
            current.thunderLevel = 0.0f;
            break;
        default:
            break;
        }
    } else if (auto dimension = std::dynamic_pointer_cast<ChangeDimensionPacket>(packet)) {
        mesher->clear();
        {
            std::lock_guard<std::mutex> guard(mutex);
            for (auto& update : pendingUpdates) {
                update.mesh.reset();
                update.credit.reset();
            }
        }
        world.changeDimension(dimension->mDimension, floorChunk(dimension->mPosition.x), floorChunk(dimension->mPosition.z));
        motionDimension = dimension->mDimension;
        motion.teleport({ dimension->mPosition.x, dimension->mPosition.y - EyeHeight, dimension->mPosition.z });
        motionHistory.clear();
        serverMotions.clear();
        motionStarted = false;
        actors.clear();
        runtimeByUnique.clear();
        std::vector<std::string> worn;
        for (const auto& [runtime, uuid] : uuidByRuntime) {
            worn.push_back(uuid);
        }
        uuidByRuntime.clear();
        dimensionAckReceived = false;
        dimensionSpawnReceived = false;
        for (const std::string& uuid : worn) {
            releaseSkin(uuid);
        }
        std::lock_guard<std::mutex> guard(mutex);
        pendingParticles.clear();
        std::erase_if(pendingCameraEvents, [](const auto& event) { return dynamic_cast<const CameraPresetsPacket*>(event.get()) == nullptr; });
        current.cameraFov = {};
        current.dimension = dimension->mDimension;
        current.changingDimension = true;
        startingTerrain = true;
    } else if (auto action = std::dynamic_pointer_cast<PlayerActionPacket>(packet)) {
        if (action->mAction == PlayerActionType::DimensionChangeSuccess) {
            dimensionAckReceived = true;
        }
    } else if (auto camera = std::dynamic_pointer_cast<CameraInstructionPacket>(packet)) {
        {
            std::lock_guard<std::mutex> guard(mutex);
            pendingCameraEvents.push_back(camera);
        }
        if (camera->mHasFovInstruction) {
            const CameraFovInstruction& fov = camera->mFovInstruction;
            std::lock_guard<std::mutex> guard(mutex);
            ++current.cameraFov.serial;
            current.cameraFov.degrees = fov.mFov;
            current.cameraFov.easeSeconds = fov.mEaseTime;
            current.cameraFov.easeType = static_cast<int>(fov.mEaseType);
            current.cameraFov.clear = fov.mClear;
        }
    } else if (std::dynamic_pointer_cast<CameraPresetsPacket>(packet) || std::dynamic_pointer_cast<CameraShakePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        pendingCameraEvents.push_back(packet);
    }
}

/**
 * Queues an effect for the client, dropping it once the queue is full so a
 * client that stopped taking them does not grow it forever.
 */
void Session::queueParticle(world::ParticleSpawn spawn)
{
    constexpr size_t MaxPendingParticles = 1024;
    std::lock_guard<std::mutex> guard(mutex);
    if (pendingParticles.size() < MaxPendingParticles) {
        pendingParticles.push_back(std::move(spawn));
    }
}

std::vector<world::ParticleSpawn> Session::takeParticles()
{
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (!guard.owns_lock()) return {};
    std::vector<world::ParticleSpawn> spawns = std::move(pendingParticles);
    pendingParticles.clear();
    return spawns;
}

/**
 * Once the server has said the new dimension is ready and the chunks around
 * the player are here, answers its acknowledgement and lifts the dimension
 * screen. Proxies like WaterdogPE switch servers without sending a single
 * chunk and settle it with a spawn status instead, which counts too.
 */
void Session::finishDimensionChange()
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!current.changingDimension) {
            return;
        }
    }
    bool ready = dimensionSpawnReceived || (dimensionAckReceived && world.centerLoaded());
    if (!ready || !connection) {
        return;
    }
    PlayerActionPacket action;
    action.mRuntimeActorId = static_cast<int64_t>(localRuntimeId);
    action.mAction = PlayerActionType::DimensionChangeSuccess;
    action.mFace = -1;
    transmit(action);
    dimensionAckReceived = false;
    dimensionSpawnReceived = false;
    std::lock_guard<std::mutex> guard(mutex);
    current.changingDimension = false;
}

void Session::scheduleMeshes()
{
    applyHiddenBlocks();
    if (!assets) {
        return;
    }
    std::vector<world::SubChunkKey> dirty = world.store().takeDirty();
    std::set<world::SubChunkKey> urgent = world.store().takeUrgent();
    // Sub-chunks waiting on a neighbour column only come back when a column arrived or now and then, not on every pass.
    const double now = secondsNow();
    size_t columns = world.store().columnCount();
    if (!frontierMeshes.empty() && (columns != frontierColumns || now >= frontierRecheck)) {
        size_t taken = dirty.size();
        for (const auto& [key, since] : frontierMeshes) {
            if (!std::binary_search(dirty.begin(), dirty.begin() + taken, key)) dirty.push_back(key);
        }
        frontierRecheck = now + FrontierRecheckSeconds;
    }
    frontierColumns = columns;
    // Coalesce neighbour refreshes at the loading edge without delaying the first visible terrain.
    auto neighboursLoaded = [&](const world::SubChunkKey& key) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (int32_t dz = -1; dz <= 1; ++dz) {
                if (!world.store().isLoaded({ key.dimension, key.x + dx, key.z + dz })) return false;
            }
        }
        return true;
    };
    auto waitsForNeighbours = [&](const world::SubChunkKey& key) {
        if (world.settled() || neighboursLoaded(key)) {
            frontierMeshes.erase(key);
            return false;
        }
        auto [entry, inserted] = frontierMeshes.try_emplace(key, now);
        if (now - entry->second < FrontierWaitSeconds) return true;
        frontierMeshes.erase(entry);
        return false;
    };
    MotionVector feet = motion.position();
    bool startup;
    {
        std::lock_guard<std::mutex> guard(mutex);
        startup = startingTerrain;
    }
    world::MeshViewPriority priority({ feet.x, feet.y, feet.z }, lookDirection, startup);
    mesher->setView(priority);
    struct RankedChunk {
        world::SubChunkKey key;
        world::MeshPriority priority;
    };
    thread_local std::vector<RankedChunk> ranked;
    ranked.clear();
    ranked.reserve(dirty.size());
    for (const auto& key : dirty) {
        ranked.push_back({ key, priority.rank(key.x, key.y, key.z, urgent.contains(key), meshedGenerations.contains(key)) });
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.priority < right.priority;
    });
    size_t admitted = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);

    static constexpr int32_t Offsets[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    for (const auto& entry : ranked) {
        const world::SubChunkKey& key = entry.key;
        std::shared_ptr<const world::SubChunk> center = world.store().subChunk(key);
        bool startupTerrain = priority.isStartup(key.x, key.z);
        if (center && meshedGenerations.contains(key) && !urgent.contains(key) && !startupTerrain && waitsForNeighbours(key)) {
            continue;
        }
        if (startupTerrain) frontierMeshes.erase(key);
        if (center && (admitted >= world::MeshScheduler::QueueCapacity
            || (admitted && std::chrono::steady_clock::now() >= deadline) || !mesher->canSubmit(key, urgent.contains(key), meshedGenerations.contains(key)))) {
            world.store().deferDirty(key, urgent.contains(key));
            continue;
        }
        // Deferring admission must not repeatedly cancel a queued or running mesh.
        uint64_t generation = ++nextMeshGeneration;
        if (!center) {
            mesher->invalidate(key, generation);
            frontierMeshes.erase(key);
            mesher->cancel(key);
            meshGenerations.erase(key);
            meshedGenerations.erase(key);
            auto existing = meshes.find(key);
            if (existing != meshes.end()) {
                meshQuads -= existing->second->quadCount();
                meshes.erase(existing);
                std::lock_guard<std::mutex> guard(mutex);
                std::erase_if(pendingUpdates, [&](const MeshUpdate& update) { return update.key == key; });
                pendingUpdates.push_back({ key, nullptr });
            }
            continue;
        }
        world::DimensionRange range;
        if (!world::vanillaDimensionRange(key.dimension, range)) {
            range = { key.y, 32 };
        }
        world::MeshInput input;
        input.center = std::move(center);
        for (size_t face = 0; face < 6; ++face) {
            input.neighbours[face] = world.store().subChunk({ key.dimension, key.x + Offsets[face][0], key.y + Offsets[face][1], key.z + Offsets[face][2] });
        }
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                input.biomes[size_t((dz + 1) * 3 + (dx + 1))] = world.store().biomes({ key.dimension, key.x + dx, key.y, key.z + dz });
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    input.around[size_t((dx + 1) * 9 + (dy + 1) * 3 + (dz + 1))] = world.store().subChunk({ key.dimension, key.x + dx, key.y + dy, key.z + dz });
                }
                for (int32_t y = key.y + 2; y < range.baseSubChunkY + range.subChunkCount; ++y) {
                    if (std::shared_ptr<const world::SubChunk> above = world.store().subChunk({ key.dimension, key.x + dx, y, key.z + dz })) {
                        input.above[size_t((dx + 1) * 3 + (dz + 1))].push_back(std::move(above));
                    }
                }
            }
        }
        for (const std::shared_ptr<const world::SubChunk>& around : input.around) {
            hideNewValues(around.get());
        }
        input.skyLight = key.dimension == 0;
        input.blockEntities = world.store().blockEntities(key);
        input.origin = { key.x * 16, key.y * 16, key.z * 16 };
        world::MeshDeferred displaced { key };
        if (mesher->submit(key, generation, std::move(input), assets, ids, urgent.contains(key), &displaced, meshedGenerations.contains(key))) {
            meshGenerations.insert_or_assign(key, generation);
            if (displaced.key != key) world.store().deferDirty(displaced.key, displaced.urgent);
            ++admitted;
        } else {
            world.store().deferDirty(key, urgent.contains(key));
        }
    }
}

void Session::collectMeshes()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    for (size_t count = 0; count < world::MeshScheduler::ResultCapacity; ++count) {
        if (count && std::chrono::steady_clock::now() >= deadline) break;
        auto ready = mesher->takeResults(1);
        if (ready.empty()) break;
        world::MeshResult& result = ready.front();
        if (!mesher->isCurrent(result)) {
            continue;
        }
        auto generation = meshGenerations.find(result.key);
        if (generation == meshGenerations.end() || generation->second != result.generation) {
            continue;
        }
        meshedGenerations.insert_or_assign(result.key, result.generation);
        auto existing = meshes.find(result.key);
        bool hadMesh = existing != meshes.end();
        if (hadMesh) {
            meshQuads -= existing->second->quadCount();
            meshes.erase(existing);
        }

        const std::vector<world::Material>& materials = assets->materials();
        auto word = [&](uint32_t id) {
            return (id < materials.size() ? materials[id] : materials.front()).gpuWord();
        };
        for (auto* cubes : { &result.mesh.cubes, &result.mesh.translucentCubes }) {
            for (world::PackedQuad& quad : *cubes) {
                quad.material = word(quad.material);
            }
        }
        for (auto* models : { &result.mesh.models, &result.mesh.translucentModels }) {
            for (world::ModelQuadGpu& quad : *models) {
                quad.words[10] = word(quad.words[10]);
            }
        }

        std::shared_ptr<const world::ChunkMesh> mesh;
        if (!result.mesh.empty()) {
            meshQuads += result.mesh.quadCount();
            mesh = std::make_shared<const world::ChunkMesh>(std::move(result.mesh));
            meshes.emplace(result.key, mesh);
        }
        if (mesh || hadMesh) {
            std::lock_guard<std::mutex> guard(mutex);
            bool refresh = result.refresh;
            for (const auto& update : pendingUpdates) {
                if (update.key == result.key) refresh &= update.refresh;
            }
            std::erase_if(pendingUpdates, [&](const MeshUpdate& update) { return update.key == result.key; });
            pendingUpdates.push_back({ result.key, std::move(mesh), std::move(result.credit), result.urgent, refresh });
        }
    }
}

bool Session::localTerrainReady()
{
    MotionVector feet = motion.position();
    int32_t dimension = current.dimension;
    int32_t centerX = static_cast<int32_t>(std::floor(feet.x)) >> 4;
    int32_t centerZ = static_cast<int32_t>(std::floor(feet.z)) >> 4;
    std::vector<world::SubChunkKey> sections;
    for (int32_t dx = -1; dx <= 1; ++dx) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
            world::ChunkKey column { dimension, centerX + dx, centerZ + dz };
            if (!world.store().isLoaded(column) || world.columnPending(column)) {
                return false;
            }
            for (const world::SubChunkKey& key : world.store().sectionsOf(column)) {
                if (world.store().isDirty(key)) {
                    return false;
                }
                auto scheduled = meshGenerations.find(key);
                if (scheduled != meshGenerations.end()) {
                    auto meshed = meshedGenerations.find(key);
                    if (meshed == meshedGenerations.end() || meshed->second != scheduled->second) {
                        return false;
                    }
                }
                sections.push_back(key);
            }
        }
    }
    std::lock_guard<std::mutex> guard(mutex);
    for (const MeshUpdate& update : pendingUpdates) {
        if (std::find(sections.begin(), sections.end(), update.key) != sections.end()) {
            return false;
        }
    }
    return true;
}

void Session::fail(const std::string& error)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled) {
        current.state = SessionState::Idle;
        publishSnapshotLocked();
        return;
    }
    current.state = SessionState::Failed;
    current.error = error;
    publishSnapshotLocked();
}

void Session::run(std::string target, MinecraftAuthentication* authentication, std::string offlineName)
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        snapshotProducerThread = std::this_thread::get_id();
    }
    try {
        std::optional<std::string> next = std::move(target);
        while (next && !cancelled) {
            std::string address = std::move(*next);
            next = join(address, authentication, offlineName);
            if (next) {
                debugLog("transfer to " + *next);
                std::string name;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    name = current.name;
                }
                resetSnapshot(std::move(name), *next);
            }
        }
    } catch (const std::exception& error) {
        fail(error.what());
    } catch (...) {
        fail("Unexpected session failure");
    }
    world.cancelDecoding();
    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled && (current.state == SessionState::Joined || current.state == SessionState::Connecting || current.state == SessionState::Resolving)) {
        current.state = SessionState::Idle;
    }
    publishSnapshotLocked();
    snapshotProducerThread = {};
}

std::optional<std::string> Session::join(const std::string& target, MinecraftAuthentication* authentication, const std::string& offlineName)
{
    transferTarget.reset();
    ClientConnectionSettings settings;
    settings.mProtocolVersion = ProtocolVersion;
    settings.mGameVersion = GameVersion;
    settings.mAuthentication = authentication;
    settings.mIdentity.mDisplayName = offlineName;
    applyDefaultSkin(settings.mClientData);
    settings.mChunkRadius = requestedRadius.load();
    settings.mTimeoutMs = TimeoutMs;
    settings.mCancel = &cancelled;
    packDecision = static_cast<int>(ResourcePackDecision::Pending);
    settings.mResourcePacks.mIsCached = [this](const ResourcePackOffer& offer) {
        std::filesystem::path path = packPath(offer.mPackId, offer.mPackVersion);
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return false;
        }
        if (offer.mPackSize && std::filesystem::file_size(path, error) != offer.mPackSize) return false;
        if (error) return false;
        std::shared_ptr<const world::PackFiles> pack = cachedPack(path.string());
        if (!pack) {
            std::string packError;
            pack = world::loadServerPack(path, offer.mContentKey, packError);
            if (!pack || !packMatches(*pack, offer)) {
                debugLog("pack cache rejected " + offer.mPackId + ": " + (packError.empty() ? "manifest mismatch" : packError));
                return false;
            }
            std::lock_guard<std::mutex> guard(mutex);
            cachePackLocked(path.string(), pack);
        }
        if (auto title = packTitle(*pack)) {
            std::lock_guard<std::mutex> guard(mutex);
            if (!current.titleImage) {
                current.titleImage = std::move(title);
                publishSnapshotLocked();
            }
        }
        return true;
    };
    settings.mResourcePacks.mValidate = [this](const DownloadedResourcePack& pack, std::string& error) {
        auto files = world::loadServerPackData(pack.mData, pack.mOffer.mContentKey, error);
        if (!files || !packMatches(*files, pack.mOffer)) {
            if (error.empty()) error = "manifest UUID or version does not match the server offer";
            error = "Resource pack " + pack.mOffer.mPackId + ": " + error;
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex);
        cachePackLocked(packPath(pack.mOffer.mPackId, pack.mOffer.mPackVersion).string(), std::move(files));
        return true;
    };
    settings.mResourcePacks.mOffer = [this](const std::vector<ResourcePackOffer>& offers) {
        std::lock_guard<std::mutex> guard(mutex);
        current.packPrompt = true;
        current.packCount = offers.size();
        current.packBytes = 0;
        current.packSkippable = true;
        for (const ResourcePackOffer& offer : offers) {
            current.packBytes += offer.mPackSize;
            current.packSkippable = current.packSkippable && !offer.mRequired;
        }
        publishSnapshotLocked();
    };
    settings.mResourcePacks.mDecision = [this]() {
        return static_cast<ResourcePackDecision>(packDecision.load());
    };
    settings.mResourcePacks.mProgress = [this](uint64_t received, uint64_t total) {
        std::lock_guard<std::mutex> guard(mutex);
        current.packDownloading = true;
        current.packReceived = received;
        current.packTotal = total;
        publishSnapshotLocked();
    };

    std::string inviteCode = RealmsService::inviteCode(target);
    std::unique_ptr<MultiplayerSessionDirectory> multiplayerSession;
    if (target.rfind(SessionHandlePrefix, 0) == 0) {
        if (!authentication) {
            fail("Sign in with Microsoft to join your friends");
            return std::nullopt;
        }
        std::string handle = target.substr(std::char_traits<char>::length(SessionHandlePrefix));
        multiplayerSession = std::make_unique<MultiplayerSessionDirectory>(*authentication);
        SessionConnectionTarget resolved;
        std::string error;
        if (!multiplayerSession->join(handle, TimeoutMs, &cancelled, resolved, error)) {
            debugLog("friend session join failed");
            fail("Could not join your friend's world: " + error);
            return std::nullopt;
        }
        resolved.applyTo(settings);
        std::lock_guard<std::mutex> guard(mutex);
        current.state = SessionState::Connecting;
        publishSnapshotLocked();
    } else if (target.rfind(RealmPrefix, 0) == 0 || !inviteCode.empty()) {
        if (!authentication) {
            fail("Sign in with Microsoft to join Realms");
            return std::nullopt;
        }
        RealmsService realms(*authentication);
        realms.setCancelFlag(&cancelled);
        long long realmId = 0;
        if (!inviteCode.empty()) {
            std::string error;
            if (!acceptRealmInvite(realms, inviteCode, realmId, error)) {
                fail(error);
                return std::nullopt;
            }
        } else {
            realmId = std::strtoll(target.c_str() + std::char_traits<char>::length(RealmPrefix), nullptr, 10);
        }
        RealmAddress address;
        SessionConnectionTarget resolved;
        std::string error;
        if (!realms.requestAddress(realmId, TimeoutMs, &cancelled, address, error) || !address.toTarget(resolved, error)) {
            fail(error);
            return std::nullopt;
        }
        resolved.applyTo(settings);
        std::lock_guard<std::mutex> guard(mutex);
        current.state = SessionState::Connecting;
        publishSnapshotLocked();
    } else if (target.rfind(ExperiencePrefix, 0) == 0) {
        if (!authentication) {
            fail("Sign in with Microsoft to join featured servers");
            return std::nullopt;
        }
        std::string error;
        if (!resolveExperience(*authentication, target.substr(std::char_traits<char>::length(ExperiencePrefix)), settings.mHost, settings.mPort, error)) {
            fail(error);
            return std::nullopt;
        }
        std::lock_guard<std::mutex> guard(mutex);
        current.state = SessionState::Connecting;
        publishSnapshotLocked();
    } else if (!parseHostPort(target, settings.mHost, settings.mPort)) {
        fail("Invalid server address: " + target);
        return std::nullopt;
    }

    {
        std::string host = settings.mHost.find(':') != std::string::npos ? "[" + settings.mHost + "]" : settings.mHost;
        std::lock_guard<std::mutex> guard(mutex);
        current.endpoint = host + ":" + std::to_string(settings.mPort);
        publishSnapshotLocked();
    }

    resetDebugLog();
    world::clearServerDimensionHeights();
    debugLog("dial " + settings.mHost + ":" + std::to_string(settings.mPort) + " radius " + std::to_string(settings.mChunkRadius));
    settings.mDeferSpawn = true;
    settings.mPacketObserver = [this](MinecraftPacketIds id) {
        debugLog("dial packet " + std::to_string(static_cast<int>(id)));
        journal.record(false, static_cast<int>(id), {});
        if (id == MinecraftPacketIds::ResourcePackStack) {
            std::lock_guard<std::mutex> guard(mutex);
            current.packsResolved = true;
            publishSnapshotLocked();
        }
    };
    settings.mDiagnostic = [](const std::string& message) { debugLog("network " + message); };
    ClientConnectionResult result = ClientNetworkSystem::dial(settings);
    {
        std::lock_guard<std::mutex> guard(mutex);
        current.packPrompt = false;
        current.packDownloading = false;
        publishSnapshotLocked();
    }
    for (const DownloadedResourcePack& pack : result.mResourcePacks) {
        std::string error;
        if (!savePack(pack, error)) { fail(error); return std::nullopt; }
    }
    if (!result.mConnection) {
        debugLog("dial failed: " + result.mError);
        fail(result.mError.empty() ? "Could not connect" : result.mError);
        return std::nullopt;
    }
    debugLog("dial done, chunk radius " + std::to_string(result.mConnection->getChunkRadius()) + ", spawn " + (result.mConnection->isSpawnReceived() ? "received" : "pending"));
    blockDefinitions = BlockDefinitionRegistry {};
    itemDefinitions = ItemDefinitionRegistry {};
    if (const std::shared_ptr<ItemRegistryPacket>& registry = result.mConnection->getItemRegistry()) {
        for (const ItemComponentEntry& entry : registry->mEntries) {
            itemDefinitions.registerDefinition(std::make_shared<ItemDefinition>(entry.mIdentifier, entry.mRuntimeId, entry.mComponentBased, entry.mComponentData));
        }
    }
    codecContext = std::make_unique<PacketCodecContext>(blockDefinitions, itemDefinitions);
    result.mConnection->setCodecContext(codecContext.get());
    inventoryModel = {};
    inventoryBefore.reset();
    inventoryChangedSlots.clear();
    pendingInventoryRequest = 0;
    inventoryRequestId = -1;
    inventoryClosing = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        inventoryCommands.clear();
        maps.clear();
        requestedMaps.clear();
        pendingMapRequests.clear();
    }
    requestedSlot = -1;
    spawnInitialized = false;
    motion = PlayerMotion {};
    motionHistory.clear();
    serverMotions.clear();
    motionStarted = false;
    teleportHandled = false;
    missedSwing = false;
    clientTick = 0;
    nextMotionTick = 0.0;
    lastMotionInput = MotionInput {};
    movementInputLocks = 0;
    if (result.mConnection->isSpawnReceived()) {
        initializeLocalPlayer(*result.mConnection, result.mConnection->getStartGame() ? result.mConnection->getStartGame()->mRuntimeActorId : 0);
    }

    {
        std::lock_guard<std::mutex> guard(mutex);
        connection = std::move(result.mConnection);
        current.state = SessionState::Joined;
        current.displayName = result.mIdentity.mDisplayName;
        localXuid = result.mIdentity.mXuid;
        current.chunkRadius = connection->getChunkRadius();
        current.joinCount = ++joins;
        current.cameraFov = {};
        pendingUpdates.clear();
        current.serverBoomFraction = 0.0;
        pendingCameraEvents.clear();
        pendingSkins.clear();
        actors.clear();
        runtimeByUnique.clear();
        uuidByRuntime.clear();
        skinByUuid.clear();
        knownSkins.clear();
        uploadedSkinPrints.clear();
        playerNames.clear();
        playerNamesByActor.clear();
        objectives.clear();
        displaySlots.clear();
        scores.clear();
        slotOwners = {};
        // The slots were just handed back, so the old slot may soon hold somebody else's skin.
        current.localSkinSlot = NoSkin;
        current.localSlim = false;
        if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
            current.spawnX = startGame->mPlayerPosition.x;
            current.spawnY = startGame->mPlayerPosition.y;
            current.spawnZ = startGame->mPlayerPosition.z;
            current.spawnPitch = startGame->mRotation.x;
            current.spawnYaw = startGame->mRotation.y;
            current.hashedIds = startGame->mBlockNetworkIdsHashed;
            localRuntimeId = startGame->mRuntimeActorId;
            localUniqueId = startGame->mUniqueActorId;
            current.localUniqueActorId = localUniqueId;
            current.localRuntimeId = localRuntimeId;
            localUuid.clear();
            current.hud = HudState {};
            current.hud.gameType = static_cast<int32_t>(startGame->mPlayerGameType == GameType::Default ? startGame->mLevelGameType : startGame->mPlayerGameType);
            motionDimension = startGame->mDimensionId;
            motion.reset({ startGame->mPlayerPosition.x, startGame->mPlayerPosition.y - EyeHeight, startGame->mPlayerPosition.z });
            motion.setGameType(current.hud.gameType);
            MotionVector feet = motion.position();
            current.player = PlayerView {};
            current.player.previous = { feet.x, feet.y, feet.z };
            current.player.current = current.player.previous;
            current.player.tickTime = secondsNow();
            current.levelName = startGame->mLevelName;
            current.gameMode = gameModeName(startGame->mPlayerGameType);
            current.dimension = startGame->mDimensionId;
            current.worldTimeStamp = secondsNow();
            current.rainLevel = std::isfinite(startGame->mRainLevel) ? std::clamp(startGame->mRainLevel, 0.0f, 1.0f) : 0.0f;
            current.thunderLevel = std::isfinite(startGame->mLightningLevel) ? std::clamp(startGame->mLightningLevel, 0.0f, 1.0f) : 0.0f;
            for (const GameRuleData& rule : startGame->mGamerules) {
                if (rule.mType == GameRuleData::Type::Bool && util::lowercase(rule.mName) == "dodaylightcycle") {
                    current.daylightCycle = rule.mBoolValue;
                } else if (rule.mType == GameRuleData::Type::Bool && util::lowercase(rule.mName) == "showcoordinates") {
                    current.showCoordinates = rule.mBoolValue;
                }
            }
            current.worldTime = !current.daylightCycle && startGame->mDayCycleStopTime >= 0 ? startGame->mDayCycleStopTime : startGame->mCurrentTick;
            debugLog("start game time " + std::to_string(startGame->mCurrentTick) + ", lock time " + std::to_string(startGame->mDayCycleStopTime) + ", daylight cycle " + (current.daylightCycle ? "on" : "off"));
            char position[96];
            std::snprintf(position, sizeof(position), "%.1f, %.1f, %.1f", startGame->mPlayerPosition.x, startGame->mPlayerPosition.y, startGame->mPlayerPosition.z);
            current.position = position;
        }
        publishSnapshotLocked();
    }

    if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
        world.reset(startGame->mDimensionId, floorChunk(startGame->mPlayerPosition.x), floorChunk(startGame->mPlayerPosition.z));
        hashedNetworkIds = startGame->mBlockNetworkIdsHashed;
    }
    world.setChunkRadius(connection->getChunkRadius());
    sentRadius = settings.mChunkRadius;

    std::string assetsError;
    std::vector<std::shared_ptr<const world::PackFiles>> packs;
    std::map<std::string, std::shared_ptr<const world::PackFiles>> usedPacks;
    uint64_t expandedPackBytes = 0;
    uint64_t archivedPackBytes = 0;
    std::string wantedAssets;
    for (const ResourcePackOffer& offer : result.mOfferedPacks) {
        std::filesystem::path path = packPath(offer.mPackId, offer.mPackVersion);
        std::shared_ptr<const world::PackFiles> pack = cachedPack(path.string());
        if (!pack) {
            std::error_code exists;
            if (!std::filesystem::exists(path, exists)) {
                fail("Resource pack is missing from cache: " + offer.mPackId);
                return std::nullopt;
            }
            std::string key = offer.mContentKey;
            if (key.empty()) {
                std::ifstream keyFile(std::filesystem::path(path).replace_extension(".key"));
                std::getline(keyFile, key);
            }
            std::string packError;
            pack = world::loadServerPack(path, key, packError);
            if (!pack) {
                fail("Resource pack " + offer.mPackId + ": " + packError);
                return std::nullopt;
            }
        }
        constexpr uint64_t MaxStackArchive = 512ull * 1024 * 1024;
        if (pack->archiveBytes() > MaxStackArchive - archivedPackBytes) {
            fail("Resource pack stack archives exceed memory budget");
            return std::nullopt;
        }
        archivedPackBytes += pack->archiveBytes();
        constexpr uint64_t MaxStackExpanded = 1024ull * 1024 * 1024;
        if (pack->expandedBytes() > MaxStackExpanded - expandedPackBytes) {
            fail("Resource pack stack expansion exceeds memory budget");
            return std::nullopt;
        }
        expandedPackBytes += pack->expandedBytes();
        usedPacks[path.string()] = pack;
        pack = pack->withSubPack(offer.mSubPackName);
        std::shared_ptr<const std::vector<uint8_t>> title = packTitle(*pack);
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (title && !current.titleImage) {
                current.titleImage = std::move(title);
                publishSnapshotLocked();
            }
        }
        wantedAssets += path.string() + ':' + offer.mSubPackName + '\n';
        packs.push_back(std::move(pack));
    }
    {
        std::lock_guard<std::mutex> guard(mutex);
        packCache = std::move(usedPacks);
    }
    std::vector<world::CustomBlock> customBlocks;
    if (const std::shared_ptr<StartGamePacket>& startGame = connection->getStartGame()) {
        BinaryStream definitions;
        for (const BlockPropertyData& block : startGame->mBlockProperties) {
            customBlocks.push_back({ block.mName, block.mProperties });
            definitions.put(block.mName + '\n');
            NbtIo::writeTag(definitions, block.mProperties, NbtVariant::LittleEndian);
        }
        wantedAssets += definitions.getBuffer();
    }
    sessionServerPacks = packs;
    sessionCustomBlocks = customBlocks;
    ++resourceGeneration;
    {
        std::lock_guard<std::mutex> guard(mutex);
        packs.insert(packs.end(), requestedGlobalPacks.begin(), requestedGlobalPacks.end());
        appliedGlobalRevision = requestedGlobalRevision;
        current.globalResourcesRevision = appliedGlobalRevision;
        current.resourceReloading = false;
        current.resourceReloadError.clear();
        current.reloadedMeshes.reset();
        wantedAssets += "global:" + std::to_string(appliedGlobalRevision);
    }
    uint64_t combinedArchiveBytes = 0, combinedExpandedBytes = 0;
    for (const auto& pack : packs) {
        combinedArchiveBytes += pack->archiveBytes();
        combinedExpandedBytes += pack->expandedBytes();
    }
    if (combinedArchiveBytes > 512ull * 1024 * 1024 || combinedExpandedBytes > 1024ull * 1024 * 1024) {
        fail("Combined resource pack stack exceeds memory budget");
        return std::nullopt;
    }
    // Transfers inside a network usually land on the same packs, and rebuilding the atlas costs seconds.
    if (!assets || wantedAssets != assetsKey) {
        std::string buildError;
        assets = world::BlockAssets::create(packs, customBlocks, buildError);
        assetsError = buildError;
        assetsKey = assets && buildError.empty() ? std::move(wantedAssets) : std::string();
    } else {
        debugLog("reusing block assets");
    }
    ids = world::IdMapping {};
    ids.hashed = hashedNetworkIds;
    hiddenChecked.clear();
    size_t customCount = 0;
    size_t customPermutationCount = 0;
    if (assets) {
        customCount = assets->customBlockCount();
        customPermutationCount = assets->customStateCount();
        ids.sequential = assets->sequentialMap();
    }
    configurePaletteResolver();
    if (!mesher) {
        mesher = std::make_unique<world::MeshScheduler>();
    }
    mesher->clear();
    meshGenerations.clear();
    meshedGenerations.clear();
    meshes.clear();
    meshQuads = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        current.assetsError = assetsError;
        startingTerrain = true;
        current.customBlocks = customCount;
        current.customPermutations = customPermutationCount;
        current.assets = assets;
        current.packs = packs;
        if (assets) {
            current.materials = assets->materials().size();
            current.textureLayers = assets->textures().layers;
            debugLog("texture layers " + std::to_string(assets->textures().layers) + ", model templates " + std::to_string(assets->modelTemplates().size()));
            current.diagnosticVisuals = assets->diagnosticVisuals();
        }
        publishSnapshotLocked();
    }

    std::string payload;
    double lastWorldPacket = secondsNow();
    constexpr double PublicationSeconds = 1.0 / 60.0;
    double nextPublication = 0.0;
    uint64_t receivedSincePublication = 0;
    while (!cancelled && !transferTarget) {
        world.applyDecoded();
        collectViewInput();
        pollGlobalPacks();
        int waitMs = 5;
        if (spawnInitialized && nextMotionTick > 0.0) {
            waitMs = std::clamp(static_cast<int>((nextMotionTick - secondsNow()) * 1000.0), 0, 5);
        }
        bool decodeBacklogged = world.decodeBacklogged();
        if (decodeBacklogged && waitMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        bool received = !decodeBacklogged && connection->readRaw(payload, waitMs, &cancelled);
        if (received) {
            lastWorldPacket = secondsNow();
            handleWorldPacket(payload);
            ++receivedSincePublication;
            const double batchDeadline = secondsNow() + 0.004;
            for (size_t count = 1; count < 256 && !cancelled && !transferTarget && secondsNow() < batchDeadline; ++count) {
                if (world.decodeBacklogged() || !connection->receiveRaw(payload)) break;
                handleWorldPacket(payload);
                ++receivedSincePublication;
            }
        } else if (connection->isClosed()) {
            debugLog("connection closed: " + connection->getDisconnectReason() + "; last world packet "
                     + std::to_string(secondsNow() - lastWorldPacket) + " seconds ago; spawn=" + (spawnInitialized ? "initialized" : "waiting"));
            break;
        }

        world.applyDecoded();
        MotionVector requestFeet = motion.position();
        bool startupRequests;
        {
            std::lock_guard<std::mutex> guard(mutex);
            startupRequests = startingTerrain;
        }
        world::MeshViewPriority requestPriority({ requestFeet.x, requestFeet.y, requestFeet.z }, lookDirection, startupRequests);
        for (const std::unique_ptr<SubChunkRequestPacket>& request : world.takeRequests(world::WorldStream::Clock::now(), requestPriority)) {
            transmit(*request);
        }
        if (int wanted = requestedRadius.load(); wanted != sentRadius) {
            RequestChunkRadiusPacket request;
            request.mRadius = wanted;
            request.mMaxRadius = wanted;
            transmit(request);
            sentRadius = wanted;
            debugLog("requested chunk radius " + std::to_string(wanted));
        }
        if (int slot = requestedSlot.exchange(-1); slot >= 0) {
            sendSelectedSlot(slot);
        }
        std::vector<std::string> raw;
        {
            std::lock_guard<std::mutex> guard(mutex);
            raw.swap(rawOutgoing);
        }
        for (const std::string& payload : raw) {
            MinecraftPacketIds rawId;
            journal.record(true, BedrockConnection::peekPacketId(payload, rawId) ? static_cast<int>(rawId) : -1, payload);
            connection->sendRaw(payload);
        }
        if (respawnRequested.exchange(false)) {
            sendRespawnRequest();
        }
        if (attackRequested.exchange(false)) {
            interact(false);
        }
        if (int pick = pickRequested.exchange(0); pick != 0) {
            pickBlock(pick == 2);
        }
        flushInventory();
        flushChat();
        flushForms();
        tickMotion();
        collectMeshes();
        scheduleMeshes();
        finishDimensionChange();
        if (assets) {
            publishBreaking();
        }

        connection->flush();
        const double publicationNow = secondsNow();
        if (publicationNow < nextPublication) continue;
        nextPublication = publicationNow + PublicationSeconds;
        std::vector<ActorView> publishedActors;
        publishedActors.reserve(actors.size());
        for (auto actor = actors.begin(); actor != actors.end();) {
            constexpr double PickupSeconds = 0.25;
            if (actor->second.pickedUpAt > 0.0 && publicationNow - actor->second.pickedUpAt > PickupSeconds) {
                actor = actors.erase(actor);
            } else {
                publishedActors.push_back(actor->second);
                ++actor;
            }
        }
        bool localReady = localTerrainReady();
        std::lock_guard<std::mutex> guard(mutex);
        current.localTerrainReady = localReady;
        if (localReady) startingTerrain = false;
        current.packetsReceived += receivedSincePublication;
        receivedSincePublication = 0;
        current.world = world.stats();
        current.meshes = meshes.size();
        current.meshQuads = meshQuads;
        current.meshJobs = mesher->pending();
        current.cohortComplete = world.cohortLoaded();
        current.updatesPending = !pendingUpdates.empty();
        current.localRuntimeId = localRuntimeId;
        current.localUniqueActorId = localUniqueId;
        if (double now = secondsNow(); now - lastReadinessLog >= 1.0) {
            lastReadinessLog = now;
            debugLog("columns " + std::to_string(current.world.columns) + " subchunks " + std::to_string(current.world.subChunks) + " pending " + std::to_string(current.world.pendingSubChunks)
                + " meshes " + std::to_string(meshes.size()) + " jobs " + std::to_string(current.meshJobs) + " cohort " + (current.cohortComplete ? "complete" : "incomplete")
                + " updates " + (current.updatesPending ? "pending" : "none") + " radius " + std::to_string(current.chunkRadius) + " spawn " + (spawnInitialized ? "initialized" : "waiting") + " actors " + std::to_string(actors.size()) + " skins " + std::to_string(skinByUuid.size()) + " mesh ms " + std::to_string(mesher->averageMilliseconds()) + " workers " + std::to_string(mesher->workerCount()));
        }
        if (assets) {
            int32_t bx = static_cast<int32_t>(std::floor(current.spawnX));
            int32_t by = static_cast<int32_t>(std::floor(current.spawnY));
            int32_t bz = static_cast<int32_t>(std::floor(current.spawnZ));
            world::SubChunkKey probe { current.dimension, bx >> 4, by >> 4, bz >> 4 };
            if (std::shared_ptr<const world::SubChunk> sub = world.store().subChunk(probe)) {
                uint32_t value = sub->runtimeId(0, uint32_t(bx & 15), uint32_t(by & 15), uint32_t(bz & 15));
                current.blockAtPlayer = assets->describe(value, ids.hashed, ids.sequential.get());
            } else {
                current.blockAtPlayer = "no sub-chunk (air)";
            }
            current.targetBlock = traceTarget();
            current.boomFraction = boomFraction();
            current.serverBoomFraction = boomFraction(serverBoomOrigin, serverBoomDelta);
            current.cameraMedium = mediumAt(lookOrigin);
            current.airSequential = assets->airSequentialId();
            current.airHash = assets->airNetworkHash();
            current.unresolvedLookups = assets->unresolvedLookups();
            current.lastUnresolved = assets->lastUnresolvedValue();
        }
        auto publication = std::make_shared<SessionSnapshot>(current);
        publication->actors = std::move(publishedActors);
        publishSnapshotLocked(std::move(publication));
    }

    world.cancelDecoding();
    std::optional<std::string> next = std::exchange(transferTarget, std::nullopt);
    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled) {
        connection->disconnect("Disconnected");
        current.state = SessionState::Idle;
        next.reset();
    } else if (next) {
        connection->disconnect(std::string());
    } else {
        current.state = SessionState::Disconnected;
        current.error = connection->getDisconnectReason();
    }
    connection.reset();
    publishSnapshotLocked();
    return next;
}

void Session::transmit(const Packet& packet)
{
    if (packetHook && codecContext && packetHook->wantsOutbound()) {
        BinaryStream stream;
        packet.writeWithHeader(stream, *codecContext);
        std::string payload = stream.getBuffer();
        if (!packetHook->outbound(static_cast<int>(packet.getId()), payload)) {
            return;
        }
        MinecraftPacketIds id;
        journal.record(true, BedrockConnection::peekPacketId(payload, id) ? static_cast<int>(id) : -1, payload);
        connection->sendRaw(payload);
        return;
    }
    std::string payload;
    if (journal.recording() && codecContext) {
        BinaryStream stream;
        packet.writeWithHeader(stream, *codecContext);
        payload = stream.getBuffer();
    }
    journal.record(true, static_cast<int>(packet.getId()), payload);
    connection->send(packet);
}

void Session::sendRawPacket(std::string payload)
{
    std::lock_guard<std::mutex> guard(mutex);
    rawOutgoing.push_back(std::move(payload));
}

}
