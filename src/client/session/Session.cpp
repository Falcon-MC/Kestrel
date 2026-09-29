#include "SessionData.h"

#include "Core/Json/Json.h"
#include "Core/NBT/NbtIo.h"
#include "Protocol/BlockStateHasher.h"
#include "util/SkinChoice.h"
#include "BlockUpgradeSchemas.h"
#include "Core/BlockState/BlockStateUpgrader.h"
#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/BedrockConnection.h"
#include "Network/Http/HttpClient.h"
#include "Network/Client/ClientNetworkSystem.h"
#include "Network/Session/RealmsService.h"
#include "client/DebugLog.h"
#include "Network/Crypto/Base64.h"
#include "ui/Image.h"
#include "world/PackSource.h"
#include "Protocol/Packets/AddActorPacket.h"
#include "Protocol/Packets/AnimatePacket.h"
#include "Protocol/Packets/AddPlayerPacket.h"
#include "Protocol/Packets/BlockActorDataPacket.h"
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
#include "Protocol/Packets/UpdateAttributesPacket.h"
#include "Protocol/Packets/SetActorDataPacket.h"
#include "Protocol/Packets/SetLocalPlayerAsInitializedPacket.h"
#include "Protocol/Packets/SetTimePacket.h"
#include "Protocol/Packets/LevelChunkPacket.h"
#include "Protocol/Packets/MovePlayerPacket.h"
#include "Protocol/Packets/NetworkChunkPublisherUpdatePacket.h"
#include "Protocol/Packets/StartGamePacket.h"
#include "Protocol/Packets/SubChunkPacket.h"
#include "Protocol/Packets/SubChunkRequestPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"
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
    if (!snapshot.daylightCycle) {
        return static_cast<double>(snapshot.worldTime);
    }
    return static_cast<double>(snapshot.worldTime) + (secondsNow() - snapshot.worldTimeStamp) * 20.0;
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
constexpr const char* RealmsUrl = "https://bedrock.frontendlegacy.realms.minecraft-services.net";
constexpr const char* RealmsRelyingParty = "https://pocket.realms.minecraft.net/";
constexpr unsigned int TimeoutMs = 30000;
// The block outline follows the crosshair, so the loop never sleeps long enough for it to lag behind.
constexpr int OutlineRefreshMs = 10;

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
    const std::string* manifest = pack.find("manifest.json");
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
    const std::string* encoded = pack.find("textures/ui/title.png");
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
 * The code of a Realm invite link typed as a server address, such as
 * realms.gg/AbCdEf, with or without its scheme; empty for anything else.
 */
std::string realmInviteCode(std::string target)
{
    for (const char* prefix : { "https://", "http://", "www." }) {
        if (target.rfind(prefix, 0) == 0) {
            target.erase(0, std::char_traits<char>::length(prefix));
        }
    }
    constexpr std::string_view InviteHost = "realms.gg/";
    if (target.rfind(InviteHost, 0) != 0) {
        return {};
    }
    std::string code = target.substr(InviteHost.size());
    while (!code.empty() && code.back() == '/') {
        code.pop_back();
    }
    bool valid = !code.empty() && std::all_of(code.begin(), code.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
    return valid ? code : std::string();
}

/**
 * Accepts a Realm invite code the way the game does when joining through a
 * link, which makes the player a member, and gives the Realm's id.
 */
bool acceptRealmInvite(MinecraftAuthentication& authentication, const std::string& code, long long& realmId, std::string& error)
{
    XboxLiveToken token;
    if (!authentication.getXboxLiveAuthentication().requestToken(RealmsRelyingParty, token, error)) {
        error = "request realms token: " + error;
        return false;
    }
    HttpClient::Headers headers {
        { "User-Agent", "MCPE/UWP" },
        { "Client-Version", authentication.getGameVersion() },
        { "Authorization", token.getAuthorizationHeader() },
    };
    HttpResponse response;
    if (!HttpClient::post(std::string(RealmsUrl) + "/invites/v1/link/accept/" + code, headers, "", response, error, TimeoutMs)) {
        return false;
    }
    std::unique_ptr<json::Value> root = json::parse(response.mBody);
    const json::Value* id = root ? root->get("id") : nullptr;
    if (response.mStatus != 200 || !id) {
        error = response.mStatus == 404 ? "This Realm invite link is not valid" : "Could not join the Realm (status " + std::to_string(response.mStatus) + ")";
        return false;
    }
    realmId = static_cast<long long>(id->number());
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

Session::Session() = default;

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
    current.state = target.rfind(RealmPrefix, 0) == 0 || target.rfind(ExperiencePrefix, 0) == 0 || !realmInviteCode(target).empty() ? SessionState::Resolving : SessionState::Connecting;
    current.name = std::move(name);
    current.target = std::move(target);
    pendingChat.clear();
    pendingActionbar.reset();
    pendingTitles.clear();
    pendingToasts.clear();
    outgoingChat.clear();
    pendingForms.clear();
    outgoingForms.clear();
}

void Session::disconnect()
{
    cancelled = true;
    if (worker.joinable()) {
        worker.join();
    }
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state == SessionState::Joined) {
        current.state = SessionState::Idle;
    }
}

std::string_view Session::gameVersion()
{
    return GameVersion;
}

SessionSnapshot Session::snapshot() const
{
    std::lock_guard<std::mutex> guard(mutex);
    return current;
}

std::shared_ptr<const world::PackFiles> Session::cachedPack(const std::string& path)
{
    std::lock_guard<std::mutex> guard(mutex);
    auto found = packCache.find(path);
    return found == packCache.end() ? nullptr : found->second;
}

std::vector<MeshUpdate> Session::takeMeshUpdates()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<MeshUpdate> updates = std::move(pendingUpdates);
    pendingUpdates.clear();
    return updates;
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
}

void Session::setLookRay(const std::array<double, 3>& origin, const std::array<float, 3>& direction)
{
    std::lock_guard<std::mutex> guard(mutex);
    lookOrigin = origin;
    lookDirection = direction;
}

/**
 * The liquid the given point sits in: 0 for air, 1 for water, 2 for lava. A
 * liquid fills (8 - level) / 9 of its block, a falling one or one under the
 * same liquid fills it all, the way liquid surfaces are meshed.
 */
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
    if (!kind) {
        return 0;
    }
    uint8_t above = 0;
    double surface = level >= 8 || liquidAt(x, y + 1, z, above) == kind ? 1.0 : (8.0 - (level & 7)) / 9.0;
    return position[1] - static_cast<double>(y) < surface ? kind : 0;
}

void Session::setCameraBoom(const std::array<double, 3>& origin, const std::array<double, 3>& delta)
{
    std::lock_guard<std::mutex> guard(mutex);
    boomOrigin = origin;
    boomDelta = delta;
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
    constexpr double Radius = 0.2;
    constexpr double Epsilon = 0.001;
    double length = std::sqrt(boomDelta[0] * boomDelta[0] + boomDelta[1] * boomDelta[1] + boomDelta[2] * boomDelta[2]);
    if (length < 1.0e-6) {
        return 1.0;
    }
    std::array<int32_t, 3> low {};
    std::array<int32_t, 3> high {};
    for (size_t axis = 0; axis < 3; ++axis) {
        double a = boomOrigin[axis];
        double b = boomOrigin[axis] + boomDelta[axis];
        low[axis] = static_cast<int32_t>(std::floor(std::min(a, b) - Radius)) - 1;
        high[axis] = static_cast<int32_t>(std::floor(std::max(a, b) + Radius)) + 1;
    }
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    world::BlockCollisions::Lookup lookup = [this](int32_t x, int32_t y, int32_t z) {
        return motionCell(x, y, z).primary;
    };
    std::vector<world::CollisionBox> boxes;
    for (int32_t x = low[0]; x <= high[0]; ++x) {
        for (int32_t y = low[1]; y <= high[1]; ++y) {
            for (int32_t z = low[2]; z <= high[2]; ++z) {
                if (!world.store().isLoaded({ motionDimension, x >> 4, z >> 4 })) {
                    return 0.0;
                }
                if (const world::CollisionState* state = motionCell(x, y, z).primary) {
                    table.boxes(*state, x, y, z, lookup, boxes);
                }
            }
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
            shown = value.asByte() ? "\xC2\xA7atrue" : "\xC2\xA7cfalse";
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

void Session::handleWorldPacket(const std::string& payload)
{
    MinecraftPacketIds id;
    if (!BedrockConnection::peekPacketId(payload, id)) {
        return;
    }
    if (seenPackets.insert(static_cast<int>(id)).second) {
        debugLog("first world packet " + std::to_string(static_cast<int>(id)));
    }

    switch (id) {
    case MinecraftPacketIds::PlayStatus:
    case MinecraftPacketIds::BlockActorData:
    case MinecraftPacketIds::LevelChunk:
    case MinecraftPacketIds::SubChunk:
    case MinecraftPacketIds::UpdateBlock:
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
    case MinecraftPacketIds::ContainerOpen:
    case MinecraftPacketIds::ContainerClose:
    case MinecraftPacketIds::ContainerSetData:
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
    case MinecraftPacketIds::MobEffect:
    case MinecraftPacketIds::BossEvent:
    case MinecraftPacketIds::PlayerList:
    case MinecraftPacketIds::PlayerSkin:
    case MinecraftPacketIds::SetTime:
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
    case MinecraftPacketIds::Text:
    case MinecraftPacketIds::SetTitle:
    case MinecraftPacketIds::ToastRequest:
    case MinecraftPacketIds::AvailableCommands:
    case MinecraftPacketIds::SetDisplayObjective:
    case MinecraftPacketIds::SetScore:
    case MinecraftPacketIds::RemoveObjective:
    case MinecraftPacketIds::ModalFormRequest:
    case MinecraftPacketIds::ClientboundCloseForm:
    case MinecraftPacketIds::PacketViolationWarning:
    case MinecraftPacketIds::Transfer:
    case MinecraftPacketIds::SetHud:
        break;
    default:
        return;
    }

    std::shared_ptr<Packet> packet = connection->decode(payload);
    if (!packet) {
        std::string details = std::string("Could not read ") + toString(id) + " (" + std::to_string(static_cast<int>(id)) + ")"
            + "\nSize: " + std::to_string(payload.size()) + " bytes"
            + "\nError: " + connection->getLastDecodeError();
        debugLog(details);
        {
            std::lock_guard<std::mutex> guard(mutex);
            current.packetError = std::move(details);
        }
        connection->disconnect("Bad packet received from server");
        return;
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
        event && event->mEventId == static_cast<uint8_t>(EntityEventType::HurtAnimation)) {
        double now = secondsNow();
        if (auto actor = actors.find(event->mRuntimeActorId); actor != actors.end()) {
            actor->second.lastHurt = now;
        }
        if (event->mRuntimeActorId == localRuntimeId) {
            std::lock_guard<std::mutex> guard(mutex);
            if (now - current.hud.lastHurt > 0.1) current.hud.lastHurt = now;
        }
    }

    if (auto animation = std::dynamic_pointer_cast<AnimatePacket>(packet); animation && animation->mAction == AnimatePacket::Action::SwingArm) {
        if (auto actor = actors.find(animation->mRuntimeActorId); actor != actors.end()) {
            actor->second.lastSwing = secondsNow();
        }
    }

    if (auto equipment = std::dynamic_pointer_cast<MobArmorEquipmentPacket>(packet)) {
        if (auto actor = actors.find(static_cast<uint64_t>(equipment->mRuntimeActorId)); actor != actors.end()) {
            actor->second.armor = {
                hudItemOf(equipment->mHelmet).identifier,
                hudItemOf(equipment->mChestplate).identifier,
                hudItemOf(equipment->mLeggings).identifier,
                hudItemOf(equipment->mBoots).identifier,
            };
        }
    }

    if (auto levelChunk = std::dynamic_pointer_cast<LevelChunkPacket>(packet)) {
        if (world.stats().levelChunks < 8) {
            debugLog("LevelChunk received x=" + std::to_string(levelChunk->mChunkX) + " z=" + std::to_string(levelChunk->mChunkZ)
                     + " dimension=" + std::to_string(levelChunk->mDimension) + " subchunks=" + std::to_string(levelChunk->mSubChunksLength)
                     + " request-subchunks=" + (levelChunk->mRequestSubChunks ? "yes" : "no")
                     + " cached=" + (levelChunk->mCachingEnabled ? "yes" : "no") + " bytes=" + std::to_string(levelChunk->mData.size()));
        }
        world.handle(*levelChunk);
    } else if (auto subChunk = std::dynamic_pointer_cast<SubChunkPacket>(packet)) {
        world.handle(*subChunk);
    } else if (auto updateBlock = std::dynamic_pointer_cast<UpdateBlockPacket>(packet)) {
        if (updateBlock->mDataLayer == 0) {
            const Vector3i& at = updateBlock->mBlockPosition;
            answerPredictedBreak({ at.x, at.y, at.z }, updateBlock->mRuntimeId);
        }
        world.handle(*updateBlock);
    } else if (auto updateSubChunk = std::dynamic_pointer_cast<UpdateSubChunkBlocksPacket>(packet)) {
        for (const BlockChangeEntry& entry : updateSubChunk->mStandardBlocks) {
            answerPredictedBreak({ entry.mPosition.x, entry.mPosition.y, entry.mPosition.z }, entry.mRuntimeId);
        }
        world.handle(*updateSubChunk);
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
        actor.identifier = added->mIdentifier;
        actor.scale = metadataScale(added->mMetadata, 1.0f);
        applyActorMetadata(added->mMetadata, actor);
        actors[runtime] = actor;
        runtimeByUnique[added->mUniqueActorId] = runtime;
        moveActor(runtime, added->mPosition.x, added->mPosition.y, added->mPosition.z, added->mBodyRotation, added->mHeadRotation, added->mRotation.x, true, true, true);
    } else if (auto dropped = std::dynamic_pointer_cast<AddItemActorPacket>(packet)) {
        uint64_t runtime = dropped->mRuntimeActorId;
        ActorView actor;
        actor.runtimeId = runtime;
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
        if (auto actor = actors.find(static_cast<uint64_t>(data->mRuntimeActorId)); actor != actors.end()) {
            actor->second.scale = metadataScale(data->mMetadata, actor->second.scale);
            applyActorMetadata(data->mMetadata, actor->second);
        }
    } else if (auto removed = std::dynamic_pointer_cast<RemoveActorPacket>(packet)) {
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
    } else if (auto time = std::dynamic_pointer_cast<SetTimePacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        current.worldTime = time->mTime;
        current.worldTimeStamp = secondsNow();
        debugLog("set time " + std::to_string(time->mTime));
    } else if (auto rules = std::dynamic_pointer_cast<GameRulesChangedPacket>(packet)) {
        for (const ChangedGameRuleData& rule : rules->mGameRules) {
            if (rule.mName == "dodaylightcycle" && rule.mType == ChangedGameRuleType::Bool) {
                std::lock_guard<std::mutex> guard(mutex);
                current.worldTime = currentWorldTime(current);
                current.worldTimeStamp = secondsNow();
                current.daylightCycle = rule.mBoolValue;
            }
        }
    } else if (auto actor = std::dynamic_pointer_cast<BlockActorDataPacket>(packet)) {
        world.handle(*actor);
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
    } else if (auto event = std::dynamic_pointer_cast<LevelEventPacket>(packet)) {
        handleBreakingEvent(*event);
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
        world.changeDimension(dimension->mDimension, floorChunk(dimension->mPosition.x), floorChunk(dimension->mPosition.z));
        motionDimension = dimension->mDimension;
        motion.teleport({ dimension->mPosition.x, dimension->mPosition.y - EyeHeight, dimension->mPosition.z });
        motionHistory.clear();
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
        current.dimension = dimension->mDimension;
        current.changingDimension = true;
    } else if (auto action = std::dynamic_pointer_cast<PlayerActionPacket>(packet)) {
        if (action->mAction == PlayerActionType::DimensionChangeSuccess) {
            dimensionAckReceived = true;
        }
    }
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
    connection->send(action);
    dimensionAckReceived = false;
    dimensionSpawnReceived = false;
    std::lock_guard<std::mutex> guard(mutex);
    current.changingDimension = false;
}

void Session::scheduleMeshes()
{
    std::vector<world::SubChunkKey> dirty = world.store().takeDirty();
    std::set<world::SubChunkKey> urgent = world.store().takeUrgent();
    if (!assets) {
        return;
    }

    static constexpr int32_t Offsets[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    for (const world::SubChunkKey& key : dirty) {
        uint64_t generation = ++meshGenerations[key];
        std::shared_ptr<const world::SubChunk> center = world.store().subChunk(key);
        if (!center) {
            auto existing = meshes.find(key);
            if (existing != meshes.end()) {
                meshQuads -= existing->second->quadCount();
                meshes.erase(existing);
                std::lock_guard<std::mutex> guard(mutex);
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
        input.skyLight = key.dimension == 0;
        input.blockEntities = world.store().blockEntities(key);
        input.origin = { key.x * 16, key.y * 16, key.z * 16 };
        mesher->submit(key, generation, std::move(input), assets, ids, urgent.contains(key));
    }
}

void Session::collectMeshes()
{
    for (world::MeshResult& result : mesher->takeResults()) {
        auto generation = meshGenerations.find(result.key);
        if (generation == meshGenerations.end() || generation->second != result.generation) {
            continue;
        }
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
            pendingUpdates.push_back({ result.key, std::move(mesh) });
        }
    }
}

void Session::fail(const std::string& error)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (cancelled) {
        current.state = SessionState::Idle;
        return;
    }
    current.state = SessionState::Failed;
    current.error = error;
}

void Session::run(std::string target, MinecraftAuthentication* authentication, std::string offlineName)
{
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
            packCache[path.string()] = pack;
        }
        if (auto title = packTitle(*pack)) {
            std::lock_guard<std::mutex> guard(mutex);
            if (!current.titleImage) current.titleImage = std::move(title);
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
        packCache[packPath(pack.mOffer.mPackId, pack.mOffer.mPackVersion).string()] = std::move(files);
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
    };
    settings.mResourcePacks.mDecision = [this]() {
        return static_cast<ResourcePackDecision>(packDecision.load());
    };
    settings.mResourcePacks.mProgress = [this](uint64_t received, uint64_t total) {
        std::lock_guard<std::mutex> guard(mutex);
        current.packDownloading = true;
        current.packReceived = received;
        current.packTotal = total;
    };

    std::string inviteCode = realmInviteCode(target);
    if (target.rfind(RealmPrefix, 0) == 0 || !inviteCode.empty()) {
        if (!authentication) {
            fail("Sign in with Microsoft to join Realms");
            return std::nullopt;
        }
        RealmsService realms(*authentication);
        long long realmId = 0;
        if (!inviteCode.empty()) {
            std::string error;
            if (!acceptRealmInvite(*authentication, inviteCode, realmId, error)) {
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
    } else if (!parseHostPort(target, settings.mHost, settings.mPort)) {
        fail("Invalid server address: " + target);
        return std::nullopt;
    }

    resetDebugLog();
    world::clearServerDimensionHeights();
    debugLog("dial " + settings.mHost + ":" + std::to_string(settings.mPort) + " radius " + std::to_string(settings.mChunkRadius));
    settings.mDeferSpawn = true;
    settings.mPacketObserver = [this](MinecraftPacketIds id) {
        debugLog("dial packet " + std::to_string(static_cast<int>(id)));
        if (id == MinecraftPacketIds::ResourcePackStack) {
            std::lock_guard<std::mutex> guard(mutex);
            current.packsResolved = true;
        }
    };
    settings.mDiagnostic = [](const std::string& message) { debugLog("network " + message); };
    ClientConnectionResult result = ClientNetworkSystem::dial(settings);
    {
        std::lock_guard<std::mutex> guard(mutex);
        current.packPrompt = false;
        current.packDownloading = false;
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
    { std::lock_guard<std::mutex> guard(mutex); inventoryCommands.clear(); }
    requestedSlot = -1;
    spawnInitialized = false;
    motion = PlayerMotion {};
    motionHistory.clear();
    motionStarted = false;
    teleportHandled = false;
    clientTick = 0;
    nextMotionTick = 0.0;
    lastMotionInput = MotionInput {};
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
        pendingUpdates.clear();
        pendingSkins.clear();
        actors.clear();
        runtimeByUnique.clear();
        uuidByRuntime.clear();
        skinByUuid.clear();
        knownSkins.clear();
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
                if (rule.mName == "dodaylightcycle" && rule.mType == GameRuleData::Type::Bool) {
                    current.daylightCycle = rule.mBoolValue;
                }
            }
            current.worldTime = !current.daylightCycle && startGame->mDayCycleStopTime >= 0 ? startGame->mDayCycleStopTime : startGame->mCurrentTick;
            debugLog("start game time " + std::to_string(startGame->mCurrentTick) + ", lock time " + std::to_string(startGame->mDayCycleStopTime) + ", daylight cycle " + (current.daylightCycle ? "on" : "off"));
            char position[96];
            std::snprintf(position, sizeof(position), "%.1f, %.1f, %.1f", startGame->mPlayerPosition.x, startGame->mPlayerPosition.y, startGame->mPlayerPosition.z);
            current.position = position;
        }
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
        std::shared_ptr<const std::vector<uint8_t>> title = packTitle(*pack);
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (title && !current.titleImage) {
                current.titleImage = std::move(title);
            }
        }
        wantedAssets += path.string() + '\n';
        usedPacks[path.string()] = pack;
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
    size_t customCount = 0;
    size_t customPermutationCount = 0;
    if (assets) {
        customCount = assets->customBlockCount();
        customPermutationCount = assets->customStateCount();
        ids.sequential = assets->sequentialMap();
    }
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
    if (!mesher) {
        mesher = std::make_unique<world::MeshScheduler>();
    }
    mesher->clear();
    meshGenerations.clear();
    meshes.clear();
    meshQuads = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        current.assetsError = assetsError;
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
    }

    std::string payload;
    double lastWorldPacket = secondsNow();
    while (!cancelled && !transferTarget) {
        int waitMs = 50;
        if (spawnInitialized && nextMotionTick > 0.0) {
            waitMs = std::clamp(static_cast<int>((nextMotionTick - secondsNow()) * 1000.0), 1, OutlineRefreshMs);
        }
        bool received = connection->readRaw(payload, waitMs, &cancelled);
        if (received) {
            lastWorldPacket = secondsNow();
            handleWorldPacket(payload);
        } else if (connection->isClosed()) {
            debugLog("connection closed: " + connection->getDisconnectReason() + "; last world packet "
                     + std::to_string(secondsNow() - lastWorldPacket) + " seconds ago; spawn=" + (spawnInitialized ? "initialized" : "waiting"));
            break;
        }

        for (const std::unique_ptr<SubChunkRequestPacket>& request : world.takeRequests(world::WorldStream::Clock::now())) {
            connection->send(*request);
        }
        if (int wanted = requestedRadius.load(); wanted != sentRadius) {
            RequestChunkRadiusPacket request;
            request.mRadius = wanted;
            request.mMaxRadius = wanted;
            connection->send(request);
            sentRadius = wanted;
            debugLog("requested chunk radius " + std::to_string(wanted));
        }
        if (int slot = requestedSlot.exchange(-1); slot >= 0) {
            sendSelectedSlot(slot);
        }
        if (respawnRequested.exchange(false)) {
            sendRespawnRequest();
        }
        if (attackRequested.exchange(false)) {
            interact(false);
        }
        if (useRequested.exchange(false)) {
            interact(true);
        }
        if (int pick = pickRequested.exchange(0); pick != 0) {
            pickBlock(pick == 2);
        }
        flushInventory();
        flushChat();
        flushForms();
        tickMotion();
        scheduleMeshes();
        collectMeshes();
        finishDimensionChange();
        if (assets) {
            publishBreaking();
        }

        std::lock_guard<std::mutex> guard(mutex);
        if (received) {
            ++current.packetsReceived;
        }
        current.world = world.stats();
        current.meshes = meshes.size();
        current.meshQuads = meshQuads;
        current.meshJobs = mesher->pending();
        current.cohortComplete = world.cohortLoaded();
        current.updatesPending = !pendingUpdates.empty();
        current.localRuntimeId = localRuntimeId;
        current.actors.clear();
        for (auto actor = actors.begin(); actor != actors.end();) {
            constexpr double PickupSeconds = 0.25;
            if (actor->second.pickedUpAt > 0.0 && secondsNow() - actor->second.pickedUpAt > PickupSeconds) {
                actor = actors.erase(actor);
                continue;
            }
            current.actors.push_back(actor->second);
            ++actor;
        }
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
            current.cameraMedium = mediumAt(lookOrigin);
            current.airSequential = assets->airSequentialId();
            current.airHash = assets->airNetworkHash();
            current.unresolvedLookups = assets->unresolvedLookups();
            current.lastUnresolved = assets->lastUnresolvedValue();
        }
    }

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
    return next;
}

}
