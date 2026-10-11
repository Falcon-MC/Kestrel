#include "agent/SessionControl.h"

#include "Protocol/MinecraftPacketIds.h"

#include <algorithm>
#include <cmath>

namespace kestrel::agent {

namespace {

constexpr const char* ProtocolVersion = "1";

std::string accountStateName(AccountState state)
{
    switch (state) {
    case AccountState::SignedOut:
        return "signed_out";
    case AccountState::Connecting:
        return "connecting";
    case AccountState::AwaitingCode:
        return "awaiting_code";
    case AccountState::SignedIn:
        return "signed_in";
    case AccountState::Failed:
        return "failed";
    }
    return "unknown";
}

std::string chatKindName(ChatMessage::Kind kind)
{
    switch (kind) {
    case ChatMessage::Kind::Raw:
        return "raw";
    case ChatMessage::Kind::Chat:
        return "chat";
    case ChatMessage::Kind::Translation:
        return "translation";
    case ChatMessage::Kind::Popup:
        return "popup";
    case ChatMessage::Kind::JukeboxPopup:
        return "jukebox_popup";
    case ChatMessage::Kind::Tip:
        return "tip";
    case ChatMessage::Kind::System:
        return "system";
    case ChatMessage::Kind::Whisper:
        return "whisper";
    case ChatMessage::Kind::Announcement:
        return "announcement";
    case ChatMessage::Kind::WhisperJson:
        return "whisper_json";
    case ChatMessage::Kind::Json:
        return "json";
    case ChatMessage::Kind::AnnouncementJson:
        return "announcement_json";
    }
    return "unknown";
}

std::optional<InventoryAction> inventoryAction(const std::string& name)
{
    static const std::pair<const char*, InventoryAction> Names[] = {
        { "open", InventoryAction::Open },
        { "close", InventoryAction::Close },
        { "primary", InventoryAction::Primary },
        { "secondary", InventoryAction::Secondary },
        { "quick_move", InventoryAction::QuickMove },
        { "drop", InventoryAction::Drop },
        { "hotbar_swap", InventoryAction::HotbarSwap },
        { "collect", InventoryAction::Collect },
        { "distribute", InventoryAction::Distribute },
        { "creative", InventoryAction::Creative },
        { "craft", InventoryAction::Craft },
        { "select_recipe", InventoryAction::SelectRecipe },
    };
    for (const auto& [text, action] : Names) {
        if (name == text) {
            return action;
        }
    }
    return std::nullopt;
}

std::set<int> idSet(const json::Value* list)
{
    std::set<int> ids;
    if (list && list->isArray()) {
        for (const std::unique_ptr<json::Value>& item : list->mArray) {
            if (item->isNumber()) {
                ids.insert(item->integer());
            }
        }
    }
    return ids;
}

void writeVector(JsonWriter& writer, std::string_view name, double x, double y, double z)
{
    writer.key(name).beginObject().field("x", x).field("y", y).field("z", z).endObject();
}

void writeCell(JsonWriter& writer, std::string_view name, const std::array<int32_t, 3>& cell)
{
    writer.key(name).beginObject().field("x", cell[0]).field("y", cell[1]).field("z", cell[2]).endObject();
}

}

std::string hexOf(std::string_view bytes)
{
    static constexpr char Hex[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (char c : bytes) {
        text.push_back(Hex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
        text.push_back(Hex[static_cast<unsigned char>(c) & 0xF]);
    }
    return text;
}

bool bytesOfHex(std::string_view hex, std::string& out)
{
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    out.clear();
    std::string digits;
    for (char c : hex) {
        if (c != ' ' && c != '\n' && c != '\t') {
            digits.push_back(c);
        }
    }
    if (digits.size() % 2 != 0) {
        return false;
    }
    for (size_t i = 0; i < digits.size(); i += 2) {
        int high = nibble(digits[i]);
        int low = nibble(digits[i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

std::string stringParam(const Request& request, const std::string& name, const std::string& fallback)
{
    const json::Value* value = request.param(name);
    return value && value->isString() ? value->mString : fallback;
}

double numberParam(const Request& request, const std::string& name, double fallback)
{
    const json::Value* value = request.param(name);
    return value && value->isNumber() ? value->mNumber : fallback;
}

bool boolParam(const Request& request, const std::string& name, bool fallback)
{
    const json::Value* value = request.param(name);
    return value && value->mType == json::Value::Type::Boolean ? value->mBoolean : fallback;
}

SessionControl::SessionControl(Session& session, Account& account, EventLog& events, std::string mode, Connector connect)
    : session(session)
    , account(account)
    , events(events)
    , mode(std::move(mode))
    , connect(std::move(connect))
{
}

std::string SessionControl::stateName(SessionState state)
{
    switch (state) {
    case SessionState::Idle:
        return "idle";
    case SessionState::Resolving:
        return "resolving";
    case SessionState::Connecting:
        return "connecting";
    case SessionState::Joined:
        return "joined";
    case SessionState::Disconnected:
        return "disconnected";
    case SessionState::Failed:
        return "failed";
    }
    return "unknown";
}

void SessionControl::writeItem(JsonWriter& writer, const HudItem& item)
{
    writer.beginObject().field("id", item.identifier).field("count", item.count);
    if (item.aux != 0) {
        writer.field("aux", item.aux);
    }
    if (item.damage != 0) {
        writer.field("damage", item.damage);
    }
    if (!item.customName.empty()) {
        writer.field("name", item.customName);
    }
    if (item.enchanted) {
        writer.field("enchanted", true);
    }
    if (!item.lore.empty()) {
        writer.key("lore").beginArray();
        for (const std::string& line : item.lore) {
            writer.value(line);
        }
        writer.endArray();
    }
    writer.endObject();
}

void SessionControl::writeSnapshot(JsonWriter& writer, const SessionSnapshot& snapshot, const SnapshotOptions& options)
{
    writer.beginObject();
    writer.field("state", stateName(snapshot.state));
    writer.field("server", snapshot.name).field("target", snapshot.target);
    writer.field("displayName", snapshot.displayName).field("levelName", snapshot.levelName);
    writer.field("gameMode", snapshot.gameMode).field("dimension", snapshot.dimension);
    writer.field("chunkRadius", snapshot.chunkRadius).field("packetsReceived", snapshot.packetsReceived);
    writer.field("runtimeId", snapshot.localRuntimeId);
    if (!snapshot.error.empty()) {
        writer.field("error", snapshot.error);
    }
    if (!snapshot.packetError.empty()) {
        writer.field("packetError", snapshot.packetError);
    }
    if (!snapshot.assetsError.empty()) {
        writer.field("assetsError", snapshot.assetsError);
    }
    writer.field("dead", snapshot.dead);
    if (snapshot.dead) {
        writer.field("deathCause", snapshot.deathCause);
        writer.key("deathParameters").beginArray();
        for (const std::string& parameter : snapshot.deathParameters) {
            writer.value(parameter);
        }
        writer.endArray();
    }
    writer.field("changingDimension", snapshot.changingDimension);

    writer.key("packs").beginObject()
        .field("prompt", snapshot.packPrompt)
        .field("count", snapshot.packCount)
        .field("skippable", snapshot.packSkippable)
        .field("bytes", snapshot.packBytes)
        .field("downloading", snapshot.packDownloading)
        .field("received", snapshot.packReceived)
        .field("total", snapshot.packTotal)
        .field("resolved", snapshot.packsResolved)
        .endObject();

    const PlayerView& player = snapshot.player;
    writer.key("player").beginObject();
    writer.field("active", player.active);
    writeVector(writer, "feet", player.current[0], player.current[1], player.current[2]);
    writer.field("eyeHeight", player.eyeHeight());
    writer.field("onGround", player.onGround).field("sneaking", player.sneaking).field("sprinting", player.sprinting);
    writer.field("swimming", player.swimming).field("flying", player.flying).field("movementSpeed", player.movementSpeed);
    writer.field("blockAtFeet", snapshot.blockAtPlayer);
    writer.endObject();
    writeVector(writer, "spawn", snapshot.spawnX, snapshot.spawnY, snapshot.spawnZ);

    writer.key("world").beginObject()
        .field("time", snapshot.worldTime)
        .field("daylightCycle", snapshot.daylightCycle)
        .field("rain", snapshot.rainLevel)
        .field("thunder", snapshot.thunderLevel)
        .field("columns", snapshot.world.columns)
        .field("subChunks", snapshot.world.subChunks)
        .field("pendingSubChunks", snapshot.world.pendingSubChunks)
        .field("blockUpdates", snapshot.world.blockUpdates)
        .field("decodeErrors", snapshot.world.decodeErrors)
        .field("lastError", snapshot.world.lastError)
        .field("meshes", snapshot.meshes)
        .field("meshJobs", snapshot.meshJobs)
        .field("customBlocks", snapshot.customBlocks)
        .field("hashedIds", snapshot.hashedIds)
        .endObject();

    const HudState& hud = snapshot.hud;
    writer.key("stats").beginObject()
        .field("known", hud.statsKnown)
        .field("health", hud.health)
        .field("maxHealth", hud.maxHealth)
        .field("absorption", hud.absorption)
        .field("hunger", hud.hunger)
        .field("saturation", hud.saturation)
        .field("level", hud.level)
        .field("experience", hud.experience)
        .field("air", hud.air)
        .field("maxAir", hud.maxAir)
        .field("gameType", hud.gameType)
        .field("selectedSlot", hud.selectedSlot)
        .field("hiddenHud", hud.hiddenElements)
        .endObject();
    writer.key("effects").beginArray();
    double now = secondsNow();
    for (const HudEffect& effect : hud.effects) {
        writer.beginObject().field("id", effect.id).field("amplifier", effect.amplifier).field("ambient", effect.ambient);
        if (effect.expires >= 0.0) {
            writer.field("secondsLeft", std::max(0.0, effect.expires - now));
        }
        writer.endObject();
    }
    writer.endArray();
    writer.key("bossBars").beginArray();
    for (const BossBarView& bar : hud.bossBars) {
        writer.beginObject().field("id", bar.bossId).field("title", bar.title).field("progress", bar.progress).field("color", bar.color).endObject();
    }
    writer.endArray();

    if (options.inventory) {
        writer.key("inventory").beginArray();
        for (const HudItem& item : hud.inventory) {
            writeItem(writer, item);
        }
        writer.endArray();
        writer.key("armor").beginArray();
        for (const HudItem& item : hud.armor) {
            writeItem(writer, item);
        }
        writer.endArray();
        writer.key("offhand");
        writeItem(writer, hud.offhand);

        const InventoryState& container = hud.container;
        writer.key("container").beginObject()
            .field("type", static_cast<int>(container.type))
            .field("windowId", container.windowId)
            .field("size", container.containerSize)
            .field("pending", container.pending)
            .field("revision", container.revision);
        writer.key("slots").beginArray();
        for (size_t slot = 0; slot < container.slots.size(); ++slot) {
            if (container.slots[slot].empty()) {
                continue;
            }
            writer.beginObject().field("slot", slot).key("item");
            writeItem(writer, container.slots[slot]);
            writer.endObject();
        }
        writer.endArray();
        writer.key("craftable").beginArray();
        for (int recipe : container.craftable) {
            writer.value(recipe);
        }
        writer.endArray();
        writer.endObject();
    }

    if (snapshot.targetBlock) {
        writer.key("targetBlock").beginObject();
        writeCell(writer, "cell", snapshot.targetBlock->cell);
        writer.field("name", snapshot.targetBlock->name);
        writer.key("states").beginArray();
        for (const std::string& state : snapshot.targetBlock->states) {
            writer.value(state);
        }
        writer.endArray().endObject();
    }

    writer.key("sidebar").beginObject().field("visible", snapshot.sidebar.visible).field("title", snapshot.sidebar.title);
    writer.key("lines").beginArray();
    for (const auto& [text, score] : snapshot.sidebar.lines) {
        writer.beginObject().field("text", text).field("score", score).endObject();
    }
    writer.endArray().endObject();

    writer.key("players").beginArray();
    for (const std::string& name : snapshot.players) {
        writer.value(name);
    }
    writer.endArray();

    if (options.commands && snapshot.commands) {
        writer.key("commands").beginArray();
        for (const menu::ChatCommand& command : *snapshot.commands) {
            writer.beginObject().field("name", command.name).field("description", command.description).endObject();
        }
        writer.endArray();
    }

    if (options.actors) {
        std::vector<std::pair<double, const ActorView*>> near;
        for (const ActorView& actor : snapshot.actors) {
            if (actor.runtimeId == snapshot.localRuntimeId) {
                continue;
            }
            double dx = actor.x - player.current[0];
            double dy = actor.y - player.current[1];
            double dz = actor.z - player.current[2];
            double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance <= options.actorRadius) {
                near.emplace_back(distance, &actor);
            }
        }
        std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        writer.field("actorsNearby", near.size());
        writer.key("actors").beginArray();
        for (size_t i = 0; i < near.size() && i < options.actorLimit; ++i) {
            const ActorView& actor = *near[i].second;
            writer.beginObject().field("runtimeId", actor.runtimeId).field("type", actor.identifier).field("name", actor.name);
            writer.field("distance", near[i].first);
            writeVector(writer, "position", actor.x, actor.y, actor.z);
            writer.field("yaw", actor.yaw).field("headYaw", actor.headYaw).field("pitch", actor.pitch);
            writer.field("onGround", actor.onGround).field("scale", actor.scale);
            writer.field("hitboxCount", actor.hitboxes ? actor.hitboxes->size() : 0);
            writer.key("aimAssistIndices").beginArray();
            for (int32_t index : actor.aimAssistIndices) writer.value(index);
            writer.endArray();
            if (!actor.item.empty()) {
                writer.key("item");
                writeItem(writer, actor.item);
            }
            bool armored = std::any_of(actor.armor.begin(), actor.armor.end(), [](const std::string& piece) { return !piece.empty(); });
            if (armored) {
                writer.key("armor").beginArray();
                for (const std::string& piece : actor.armor) {
                    writer.value(piece);
                }
                writer.endArray();
            }
            writer.endObject();
        }
        writer.endArray();
    }
    writer.endObject();
}

void SessionControl::noteChat(const ChatMessage& message, const std::string& text)
{
    JsonWriter writer;
    writer.beginObject().field("type", chatKindName(message.kind)).field("source", message.source).field("text", text);
    writer.field("raw", message.message);
    if (!message.parameters.empty()) {
        writer.key("parameters").beginArray();
        for (const std::string& parameter : message.parameters) {
            writer.value(parameter);
        }
        writer.endArray();
    }
    writer.endObject();
    events.add("chat", writer.take());
}

void SessionControl::observe(const SessionSnapshot& snapshot)
{
    if (snapshot.state != lastState) {
        JsonWriter writer;
        writer.beginObject().field("from", stateName(lastState)).field("to", stateName(snapshot.state)).field("server", snapshot.name);
        if (!snapshot.error.empty()) {
            writer.field("error", snapshot.error);
        }
        writer.endObject();
        events.add("session", writer.take());
        lastState = snapshot.state;
    }
    if (snapshot.joinCount != lastJoin && snapshot.state == SessionState::Joined) {
        lastJoin = snapshot.joinCount;
        JsonWriter writer;
        writer.beginObject().field("levelName", snapshot.levelName).field("gameMode", snapshot.gameMode).field("dimension", snapshot.dimension).endObject();
        events.add("joined", writer.take());
    }
    if (snapshot.error != lastError) {
        lastError = snapshot.error;
        if (!lastError.empty()) {
            JsonWriter writer;
            writer.beginObject().field("message", lastError).endObject();
            events.add("error", writer.take());
        }
    }
    if (snapshot.dead != lastDead) {
        lastDead = snapshot.dead;
        JsonWriter writer;
        writer.beginObject().field("cause", snapshot.deathCause).endObject();
        events.add(lastDead ? "death" : "respawn", writer.take());
    }
    if (snapshot.dimension != lastDimension) {
        lastDimension = snapshot.dimension;
        JsonWriter writer;
        writer.beginObject().field("dimension", snapshot.dimension).endObject();
        events.add("dimension", writer.take());
    }
    if (snapshot.packPrompt != lastPackPrompt) {
        lastPackPrompt = snapshot.packPrompt;
        if (lastPackPrompt) {
            JsonWriter writer;
            writer.beginObject().field("count", snapshot.packCount).field("bytes", snapshot.packBytes).field("skippable", snapshot.packSkippable).endObject();
            events.add("pack_prompt", writer.take());
        }
    }
}

void SessionControl::writePackets(JsonWriter& writer, const Request& request)
{
    auto after = static_cast<uint64_t>(std::max(0.0, numberParam(request, "since", 0.0)));
    auto limit = static_cast<size_t>(std::clamp(numberParam(request, "limit", 200.0), 1.0, 5000.0));
    bool hex = boolParam(request, "hex", true);
    std::vector<PacketRecord> records = session.packets().since(after, limit);
    writer.beginObject().field("last", session.packets().lastSequence()).field("recording", session.packets().recording());
    writer.key("packets").beginArray();
    for (const PacketRecord& record : records) {
        writer.beginObject()
            .field("seq", record.sequence)
            .field("time", record.time)
            .field("direction", record.outbound ? "out" : "in")
            .field("id", record.id)
            .field("name", record.name)
            .field("size", record.size);
        if (hex && !record.head.empty()) {
            writer.field("head", hexOf(record.head));
        }
        writer.endObject();
    }
    writer.endArray().endObject();
}

bool SessionControl::handle(const Request& request, AgentServer& server)
{
    const std::string& method = request.method;
    JsonWriter writer;

    if (method == "hello") {
        writer.beginObject().field("kestrel", Session::gameVersion()).field("protocol", ProtocolVersion).field("mode", mode).endObject();
    } else if (method == "session.connect") {
        std::string address = stringParam(request, "address");
        if (address.empty()) {
            server.fail(request, "address is required, host:port or realm:<id>");
            return true;
        }
        connect(stringParam(request, "name", address), address);
        writer.beginObject().field("connecting", address).endObject();
    } else if (method == "session.disconnect") {
        session.disconnect();
        writer.beginObject().field("disconnected", true).endObject();
    } else if (method == "session.respawn") {
        session.requestRespawn();
        writer.beginObject().field("requested", true).endObject();
    } else if (method == "session.packs") {
        session.answerResourcePacks(boolParam(request, "accept", true));
        writer.beginObject().field("answered", true).endObject();
    } else if (method == "session.state") {
        SnapshotOptions options;
        options.inventory = boolParam(request, "inventory", true);
        options.actors = boolParam(request, "actors", true);
        options.actorRadius = numberParam(request, "actorRadius", options.actorRadius);
        options.actorLimit = static_cast<size_t>(std::clamp(numberParam(request, "actorLimit", 128.0), 0.0, 4096.0));
        options.commands = boolParam(request, "commands", false);
        writeSnapshot(writer, *session.sharedSnapshot(), options);
    } else if (method == "chat.send") {
        std::string text = stringParam(request, "text");
        if (text.empty()) {
            server.fail(request, "text is required");
            return true;
        }
        session.sendChat(text);
        writer.beginObject().field("sent", text).endObject();
    } else if (method == "hotbar.select") {
        int slot = static_cast<int>(numberParam(request, "slot", -1));
        if (slot < 0 || slot > 8) {
            server.fail(request, "slot must be 0 to 8");
            return true;
        }
        session.selectHotbarSlot(slot);
        writer.beginObject().field("slot", slot).endObject();
    } else if (method == "interact") {
        std::string action = stringParam(request, "action");
        if (action == "attack") {
            session.requestInteraction(false);
        } else if (action == "use") {
            session.requestInteraction(true);
        } else if (action == "pick_block") {
            session.requestPickBlock(boolParam(request, "withData", false));
        } else {
            server.fail(request, "action must be attack, use or pick_block");
            return true;
        }
        writer.beginObject().field("action", action).endObject();
    } else if (method == "inventory.command") {
        std::optional<InventoryAction> action = inventoryAction(stringParam(request, "action"));
        if (!action) {
            server.fail(request, "unknown inventory action");
            return true;
        }
        InventoryCommand command;
        command.action = *action;
        command.slot = static_cast<int>(numberParam(request, "slot", -1));
        command.value = static_cast<int>(numberParam(request, "value", 0));
        command.all = boolParam(request, "all", false);
        if (const json::Value* slots = request.param("slots"); slots && slots->isArray()) {
            for (const std::unique_ptr<json::Value>& slot : slots->mArray) {
                command.slots.push_back(slot->integer());
            }
        }
        session.requestInventory(std::move(command));
        writer.beginObject().field("queued", true).endObject();
    } else if (method == "events") {
        auto after = static_cast<uint64_t>(std::max(0.0, numberParam(request, "since", 0.0)));
        auto limit = static_cast<size_t>(std::clamp(numberParam(request, "limit", 200.0), 1.0, 4000.0));
        writer.beginObject().field("last", events.last()).key("events").raw(events.since(after, limit)).endObject();
    } else if (method == "packets.configure") {
        PacketJournal::Settings settings = session.packets().settings();
        settings.recording = boolParam(request, "recording", settings.recording);
        settings.capacity = static_cast<size_t>(std::clamp(numberParam(request, "capacity", static_cast<double>(settings.capacity)), 0.0, 100000.0));
        settings.headBytes = static_cast<size_t>(std::clamp(numberParam(request, "headBytes", static_cast<double>(settings.headBytes)), 0.0, 65536.0));
        if (request.param("only")) {
            settings.only = idSet(request.param("only"));
        }
        if (request.param("ignore")) {
            settings.ignore = idSet(request.param("ignore"));
        }
        session.packets().configure(settings);
        writer.beginObject().field("recording", settings.recording).field("capacity", settings.capacity).field("headBytes", settings.headBytes).endObject();
    } else if (method == "packets.list") {
        writePackets(writer, request);
    } else if (method == "packets.stats") {
        writer.beginObject().key("packets").beginArray();
        for (const auto& [key, tally] : session.packets().tallies()) {
            writer.beginObject()
                .field("direction", key.first ? "out" : "in")
                .field("id", key.second)
                .field("name", key.second >= 0 ? toString(static_cast<MinecraftPacketIds>(key.second)) : "raw")
                .field("count", tally.count)
                .field("bytes", tally.bytes)
                .endObject();
        }
        writer.endArray().endObject();
    } else if (method == "packets.clear") {
        session.packets().clear();
        writer.beginObject().field("cleared", true).endObject();
    } else if (method == "packets.send") {
        std::string payload;
        if (!bytesOfHex(stringParam(request, "hex"), payload) || payload.empty()) {
            server.fail(request, "hex must hold the packet payload, header included");
            return true;
        }
        session.sendRawPacket(payload);
        writer.beginObject().field("queued", payload.size()).endObject();
    } else if (method == "account.state") {
        AccountSnapshot snapshot = account.snapshot();
        writer.beginObject()
            .field("state", accountStateName(snapshot.state))
            .field("gamertag", snapshot.gamertag)
            .field("xuid", snapshot.xuid)
            .field("verificationUri", snapshot.verificationUri)
            .field("userCode", snapshot.userCode)
            .field("error", snapshot.error)
            .field("realmsLoading", snapshot.realmsLoading)
            .field("realmsError", snapshot.realmsError);
        writer.key("realms").beginArray();
        for (const Realm& realm : snapshot.realms) {
            writer.beginObject().field("id", realm.id).field("name", realm.name).field("owner", realm.owner).field("open", realm.open).field("expired", realm.expired).field("address", "realm:" + std::to_string(realm.id)).endObject();
        }
        writer.endArray().endObject();
    } else if (method == "account.signIn") {
        account.signIn();
        writer.beginObject().field("started", true).endObject();
    } else if (method == "account.cancel") {
        account.cancel();
        writer.beginObject().field("cancelled", true).endObject();
    } else if (method == "account.signOut") {
        session.disconnect();
        account.signOut();
        writer.beginObject().field("signedOut", true).endObject();
    } else {
        return false;
    }
    server.respond(request, writer.take());
    return true;
}

}
