#include "modding/PacketFilters.h"

#include "client/Inventory.h"
#include "modding/ModServices.h"

#include "Protocol/Codec/ProtocolCodec.h"
#include "Protocol/Packets/AddActorPacket.h"
#include "Protocol/Packets/InventoryTransactionPacket.h"
#include "Protocol/Packets/MovePlayerPacket.h"
#include "Protocol/Packets/PlayerActionPacket.h"
#include "Protocol/Packets/PlayerAuthInputPacket.h"
#include "Protocol/Packets/TextPacket.h"
#include "Protocol/Packets/UpdateBlockPacket.h"

#include <algorithm>
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

namespace kestrel::modding {

namespace {

namespace packets = mod::packets;

constexpr int32_t InputFlagCount = static_cast<int32_t>(PlayerAuthInputData::InternalUpdate) + 1;
constexpr int32_t MaxInputFlag = 4095;
constexpr int MaxTextType = static_cast<int>(TextPacket::Type::AnnouncementJson);
constexpr int MaxMoveMode = static_cast<int>(MovePlayerMode::HeadRotation);
constexpr int MaxPlayerAction = static_cast<int>(PlayerActionType::InternalUpdate);
constexpr int MaxTransactionType = static_cast<int>(InventoryTransactionType::ItemRelease);

mod::Vec3 vecOf(const Vector3f& vector)
{
    return { vector.x, vector.y, vector.z };
}

Vector3f vectorOf(const mod::Vec3& vector)
{
    return Vector3f(static_cast<float>(vector.x), static_cast<float>(vector.y), static_cast<float>(vector.z));
}

mod::BlockPos posOf(const Vector3i& position)
{
    return { position.x, position.y, position.z };
}

Vector3i positionOf(const mod::BlockPos& position)
{
    return Vector3i(position.x, position.y, position.z);
}

bool typedId(int id)
{
    return id == packets::AuthInput::Id || id == packets::Text::Id || id == packets::MovePlayer::Id || id == packets::UpdateBlock::Id
        || id == packets::PlayerAction::Id || id == packets::AddActor::Id || id == packets::InventoryTransaction::Id;
}

/**
 * The packet's decoded fields, or monostate for a packet of another kind than
 * its id says.
 */
decltype(mod::PacketView::data) dataOf(int id, const Packet& packet)
{
    if (id == packets::AuthInput::Id) {
        const auto* input = dynamic_cast<const PlayerAuthInputPacket*>(&packet);
        if (!input) {
            return std::monostate {};
        }
        packets::AuthInput view;
        view.position = vecOf(input->mPosition);
        view.delta = vecOf(input->mDelta);
        view.pitch = input->mRotation.x;
        view.yaw = input->mRotation.y;
        view.headYaw = input->mRotation.z;
        view.moveX = input->mMotionX;
        view.moveY = input->mMotionY;
        view.tick = static_cast<uint64_t>(input->mTick);
        int32_t highest = InputFlagCount - 1;
        for (int32_t flag : input->mInputData) {
            if (flag >= 0 && flag <= MaxInputFlag) {
                highest = std::max(highest, flag);
            }
        }
        view.inputs.assign(static_cast<size_t>(highest) + 1, false);
        for (int32_t flag : input->mInputData) {
            if (flag >= 0 && flag <= MaxInputFlag) {
                view.inputs[static_cast<size_t>(flag)] = true;
            }
        }
        return view;
    }
    if (id == packets::Text::Id) {
        const auto* text = dynamic_cast<const TextPacket*>(&packet);
        if (!text) {
            return std::monostate {};
        }
        packets::Text view;
        view.type = static_cast<int>(text->mType);
        view.needsTranslation = text->mNeedsTranslation;
        view.source = text->mSourceName;
        view.message = text->mMessage;
        view.parameters = text->mParameters;
        view.xuid = text->mXuid;
        view.filteredMessage = text->mFilteredMessage;
        return view;
    }
    if (id == packets::MovePlayer::Id) {
        const auto* move = dynamic_cast<const MovePlayerPacket*>(&packet);
        if (!move) {
            return std::monostate {};
        }
        packets::MovePlayer view;
        view.runtimeId = static_cast<uint64_t>(move->mRuntimeActorId);
        view.position = vecOf(move->mPosition);
        view.pitch = move->mRotation.x;
        view.yaw = move->mRotation.y;
        view.headYaw = move->mRotation.z;
        view.mode = static_cast<int>(move->mMode);
        view.onGround = move->mOnGround;
        view.ridingRuntimeId = static_cast<uint64_t>(move->mRidingRuntimeActorId);
        view.tick = static_cast<uint64_t>(move->mTick);
        return view;
    }
    if (id == packets::UpdateBlock::Id) {
        const auto* update = dynamic_cast<const UpdateBlockPacket*>(&packet);
        if (!update) {
            return std::monostate {};
        }
        return packets::UpdateBlock { posOf(update->mBlockPosition), update->mRuntimeId, update->mFlags, update->mDataLayer };
    }
    if (id == packets::PlayerAction::Id) {
        const auto* action = dynamic_cast<const PlayerActionPacket*>(&packet);
        if (!action) {
            return std::monostate {};
        }
        return packets::PlayerAction { static_cast<uint64_t>(action->mRuntimeActorId), static_cast<int>(action->mAction), posOf(action->mBlockPosition),
            posOf(action->mResultPosition), action->mFace };
    }
    if (id == packets::AddActor::Id) {
        const auto* added = dynamic_cast<const AddActorPacket*>(&packet);
        if (!added) {
            return std::monostate {};
        }
        packets::AddActor view;
        view.uniqueId = added->mUniqueActorId;
        view.runtimeId = static_cast<uint64_t>(added->mRuntimeActorId);
        view.identifier = added->mIdentifier;
        view.position = vecOf(added->mPosition);
        view.motion = vecOf(added->mMotion);
        view.pitch = added->mRotation.x;
        view.yaw = added->mRotation.y;
        view.headYaw = added->mHeadRotation;
        view.bodyYaw = added->mBodyRotation;
        return view;
    }
    if (id == packets::InventoryTransaction::Id) {
        const auto* transaction = dynamic_cast<const InventoryTransactionPacket*>(&packet);
        if (!transaction) {
            return std::monostate {};
        }
        packets::InventoryTransaction view;
        view.transactionType = static_cast<int>(transaction->mTransactionType);
        view.actionType = transaction->mActionType;
        view.runtimeId = static_cast<uint64_t>(transaction->mRuntimeActorId);
        view.position = posOf(transaction->mBlockPosition);
        view.face = transaction->mBlockFace;
        view.hotbarSlot = transaction->mHotbarSlot;
        view.playerPosition = vecOf(transaction->mPlayerPosition);
        view.clickPosition = vecOf(transaction->mClickPosition);
        view.headPosition = vecOf(transaction->mHeadPosition);
        view.itemInHand = itemOf(hudItemOf(transaction->mItemInHand));
        return view;
    }
    return std::monostate {};
}

auto fieldsOf(const std::monostate&)
{
    return std::tuple<>();
}

auto fieldsOf(const packets::AuthInput& view)
{
    return std::tie(view.position, view.delta, view.pitch, view.yaw, view.headYaw, view.moveX, view.moveY, view.inputs, view.tick);
}

auto fieldsOf(const packets::Text& view)
{
    return std::tie(view.type, view.needsTranslation, view.source, view.message, view.parameters, view.xuid, view.filteredMessage);
}

auto fieldsOf(const packets::MovePlayer& view)
{
    return std::tie(view.runtimeId, view.position, view.pitch, view.yaw, view.headYaw, view.mode, view.onGround, view.ridingRuntimeId, view.tick);
}

auto fieldsOf(const packets::UpdateBlock& view)
{
    return std::tie(view.position, view.runtimeId, view.flags, view.layer);
}

auto fieldsOf(const packets::PlayerAction& view)
{
    return std::tie(view.runtimeId, view.action, view.position, view.resultPosition, view.face);
}

auto fieldsOf(const packets::AddActor& view)
{
    return std::tie(view.uniqueId, view.runtimeId, view.identifier, view.position, view.motion, view.pitch, view.yaw, view.headYaw, view.bodyYaw);
}

auto fieldsOf(const packets::InventoryTransaction& view)
{
    return std::tie(view.transactionType, view.actionType, view.runtimeId, view.position, view.face, view.hotbarSlot, view.playerPosition,
        view.clickPosition, view.headPosition);
}

bool unchanged(const mod::PacketView& before, const mod::PacketView& after)
{
    return std::visit([&](const auto& left) {
        using Kind = std::decay_t<decltype(left)>;
        const Kind* right = std::get_if<Kind>(&after.data);
        return right && fieldsOf(left) == fieldsOf(*right);
    }, before.data);
}

/**
 * Writes a view's fields into its packet. before is the view the packet was
 * decoded into, or nullptr for a new packet; fields a filter can set out of
 * range keep the packet's own value.
 */
void writeInto(const packets::AuthInput& view, const packets::AuthInput* before, Packet& packet)
{
    auto* input = dynamic_cast<PlayerAuthInputPacket*>(&packet);
    if (!input) {
        return;
    }
    input->mPosition = vectorOf(view.position);
    input->mDelta = vectorOf(view.delta);
    input->mRotation = Vector3f(view.pitch, view.yaw, view.headYaw);
    input->mMotionX = view.moveX;
    input->mMotionY = view.moveY;
    input->mTick = static_cast<int64_t>(view.tick);
    if (before && before->inputs == view.inputs) {
        return;
    }
    input->mInputData.clear();
    size_t count = std::min(view.inputs.size(), static_cast<size_t>(MaxInputFlag) + 1);
    for (size_t flag = 0; flag < count; ++flag) {
        if (view.inputs[flag]) {
            input->mInputData.push_back(static_cast<int32_t>(flag));
        }
    }
}

void writeInto(const packets::Text& view, const packets::Text*, Packet& packet)
{
    auto* text = dynamic_cast<TextPacket*>(&packet);
    if (!text) {
        return;
    }
    if (view.type >= 0 && view.type <= MaxTextType) {
        text->mType = static_cast<TextPacket::Type>(view.type);
    }
    text->mNeedsTranslation = view.needsTranslation;
    text->mSourceName = view.source;
    text->mMessage = view.message;
    text->mParameters = view.parameters;
    text->mXuid = view.xuid;
    text->mFilteredMessage = view.filteredMessage;
}

void writeInto(const packets::MovePlayer& view, const packets::MovePlayer*, Packet& packet)
{
    auto* move = dynamic_cast<MovePlayerPacket*>(&packet);
    if (!move) {
        return;
    }
    move->mRuntimeActorId = static_cast<int64_t>(view.runtimeId);
    move->mPosition = vectorOf(view.position);
    move->mRotation = Vector3f(view.pitch, view.yaw, view.headYaw);
    if (view.mode >= 0 && view.mode <= MaxMoveMode) {
        move->mMode = static_cast<MovePlayerMode>(view.mode);
    }
    move->mOnGround = view.onGround;
    move->mRidingRuntimeActorId = static_cast<int64_t>(view.ridingRuntimeId);
    move->mTick = static_cast<int64_t>(view.tick);
}

void writeInto(const packets::UpdateBlock& view, const packets::UpdateBlock*, Packet& packet)
{
    auto* update = dynamic_cast<UpdateBlockPacket*>(&packet);
    if (!update) {
        return;
    }
    update->mBlockPosition = positionOf(view.position);
    update->mRuntimeId = view.runtimeId;
    update->mFlags = view.flags;
    update->mDataLayer = view.layer;
}

void writeInto(const packets::PlayerAction& view, const packets::PlayerAction*, Packet& packet)
{
    auto* action = dynamic_cast<PlayerActionPacket*>(&packet);
    if (!action) {
        return;
    }
    action->mRuntimeActorId = static_cast<int64_t>(view.runtimeId);
    if (view.action >= 0 && view.action <= MaxPlayerAction) {
        action->mAction = static_cast<PlayerActionType>(view.action);
    }
    action->mBlockPosition = positionOf(view.position);
    action->mResultPosition = positionOf(view.resultPosition);
    action->mFace = view.face;
}

void writeInto(const packets::AddActor& view, const packets::AddActor*, Packet& packet)
{
    auto* added = dynamic_cast<AddActorPacket*>(&packet);
    if (!added) {
        return;
    }
    added->mUniqueActorId = view.uniqueId;
    added->mRuntimeActorId = static_cast<int64_t>(view.runtimeId);
    added->mIdentifier = view.identifier;
    added->mPosition = vectorOf(view.position);
    added->mMotion = vectorOf(view.motion);
    added->mRotation = Vector2f(view.pitch, view.yaw);
    added->mHeadRotation = view.headYaw;
    added->mBodyRotation = view.bodyYaw;
}

void writeInto(const packets::InventoryTransaction& view, const packets::InventoryTransaction*, Packet& packet)
{
    auto* transaction = dynamic_cast<InventoryTransactionPacket*>(&packet);
    if (!transaction) {
        return;
    }
    if (view.transactionType >= 0 && view.transactionType <= MaxTransactionType) {
        transaction->mTransactionType = static_cast<InventoryTransactionType>(view.transactionType);
    }
    transaction->mActionType = view.actionType;
    transaction->mRuntimeActorId = static_cast<int64_t>(view.runtimeId);
    transaction->mBlockPosition = positionOf(view.position);
    transaction->mBlockFace = view.face;
    transaction->mHotbarSlot = view.hotbarSlot;
    transaction->mPlayerPosition = vectorOf(view.playerPosition);
    transaction->mClickPosition = vectorOf(view.clickPosition);
    transaction->mHeadPosition = vectorOf(view.headPosition);
}

void writeView(const mod::PacketView& view, const mod::PacketView* before, Packet& packet)
{
    std::visit([&](const auto& fields) {
        using Kind = std::decay_t<decltype(fields)>;
        if constexpr (!std::is_same_v<Kind, std::monostate>) {
            writeInto(fields, before ? std::get_if<Kind>(&before->data) : nullptr, packet);
        }
    }, view.data);
}

int idOf(const mod::PacketView& view)
{
    return std::visit([](const auto& fields) {
        using Kind = std::decay_t<decltype(fields)>;
        if constexpr (std::is_same_v<Kind, std::monostate>) {
            return -1;
        } else {
            return Kind::Id;
        }
    }, view.data);
}

/**
 * Decodes a payload, header included, the way the connection does; nothing
 * when the codec does not know the packet or the bytes do not parse.
 */
std::shared_ptr<Packet> decodePacket(const PacketCodecContext& context, int id, const std::string& payload)
{
    const ProtocolCodec& codec = context.getCodec();
    std::shared_ptr<Packet> packet = codec.createPacket(static_cast<MinecraftPacketIds>(id));
    if (!packet) {
        return nullptr;
    }
    ReadOnlyBinaryStream stream(payload);
    try {
        packet->readHeader(stream);
        codec.read(*packet, stream, context);
    } catch (...) {
        return nullptr;
    }
    return packet;
}

std::optional<std::string> encodePacket(const PacketCodecContext& context, const Packet& packet)
{
    BinaryStream stream;
    try {
        context.getCodec().write(packet, stream, context);
    } catch (...) {
        return std::nullopt;
    }
    return stream.getBuffer();
}

}

PacketFilters::PacketFilters(ErrorSink errors)
    : errors(std::move(errors))
{
}

PacketFilters::~PacketFilters()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    for (const std::shared_ptr<Entry>& entry : entries) {
        entry->handle->expire();
    }
    for (const std::shared_ptr<Entry>& entry : typedEntries) {
        entry->handle->expire();
    }
}

mod::Subscription PacketFilters::add(size_t owner, std::shared_ptr<mod::PacketFilter> filter)
{
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->filter = std::move(filter);
    entry->handle = std::make_shared<Handle>([this, raw = entry.get()] { remove(raw); });
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (entry->filter) {
        entries.push_back(entry);
        filtering = true;
    }
    return mod::Subscription(entry->handle);
}

mod::Subscription PacketFilters::addTyped(size_t owner, std::shared_ptr<mod::TypedPacketFilter> filter)
{
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->typed = std::move(filter);
    entry->handle = std::make_shared<Handle>([this, raw = entry.get()] { remove(raw); });
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (entry->typed) {
        typedEntries.push_back(entry);
        typedFiltering = true;
    }
    return mod::Subscription(entry->handle);
}

void PacketFilters::release(size_t owner)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    auto owned = [owner](const std::shared_ptr<Entry>& entry) {
        if (entry->owner != owner) {
            return false;
        }
        entry->handle->expire();
        return true;
    };
    std::erase_if(entries, owned);
    std::erase_if(typedEntries, owned);
    refresh();
}

void PacketFilters::observe(bool inbound, bool outbound)
{
    observeInbound = inbound;
    observeOutbound = outbound;
    if (!inbound && !outbound) {
        std::lock_guard<std::mutex> guard(observedMutex);
        observed.clear();
    }
}

std::vector<ObservedPacket> PacketFilters::takeObserved()
{
    std::vector<ObservedPacket> taken;
    std::lock_guard<std::mutex> guard(observedMutex);
    taken.swap(observed);
    return taken;
}

void PacketFilters::sendTyped(const mod::PacketView& packet)
{
    if (idOf(packet) < 0) {
        return;
    }
    std::lock_guard<std::mutex> guard(outgoingMutex);
    if (outgoing.size() < OutgoingLimit) {
        outgoing.push_back(packet);
    }
}

bool PacketFilters::inbound(int id, std::string& payload)
{
    return pass(false, id, payload);
}

bool PacketFilters::outbound(int id, std::string& payload)
{
    return pass(true, id, payload);
}

bool PacketFilters::wantsOutbound() const
{
    return filtering || typedFiltering || observeOutbound;
}

void PacketFilters::attachCodec(const PacketCodecContext* context)
{
    codec = context;
}

std::vector<std::string> PacketFilters::takeOutgoing()
{
    std::vector<mod::PacketView> views;
    {
        std::lock_guard<std::mutex> guard(outgoingMutex);
        views.swap(outgoing);
    }
    std::vector<std::string> payloads;
    if (!codec) {
        return payloads;
    }
    for (const mod::PacketView& view : views) {
        std::shared_ptr<Packet> packet = codec->getCodec().createPacket(static_cast<MinecraftPacketIds>(idOf(view)));
        if (!packet) {
            continue;
        }
        writeView(view, nullptr, *packet);
        if (std::optional<std::string> payload = encodePacket(*codec, *packet)) {
            payloads.push_back(std::move(*payload));
        }
    }
    return payloads;
}

bool PacketFilters::pass(bool outbound, int id, std::string& payload)
{
    if (filtering) {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        // A filter may add or cancel filters, so walk a copy.
        std::vector<std::shared_ptr<Entry>> current = entries;
        for (const std::shared_ptr<Entry>& entry : current) {
            if (!entry->handle->active()) {
                continue;
            }
            bool keep = true;
            guarded(errors, entry->owner, [&] { keep = outbound ? entry->filter->outbound(id, payload) : entry->filter->inbound(id, payload); });
            if (!keep) {
                return false;
            }
        }
    }
    if (typedFiltering && !passTyped(outbound, id, payload)) {
        return false;
    }
    if (outbound ? observeOutbound : observeInbound) {
        std::lock_guard<std::mutex> guard(observedMutex);
        if (observed.size() < ObservedLimit) {
            observed.push_back({ outbound, id, payload });
        }
    }
    return true;
}

/**
 * Runs the typed filters over one packet. Only the packets the view knows
 * are decoded, and only a packet whose fields a filter changed is encoded
 * again, so everything else goes on byte for byte as it came.
 */
bool PacketFilters::passTyped(bool outbound, int id, std::string& payload)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    std::vector<std::shared_ptr<Entry>> current = typedEntries;
    mod::PacketView view;
    view.id = id;
    std::shared_ptr<Packet> packet;
    if (codec && typedId(id)) {
        packet = decodePacket(*codec, id, payload);
        if (packet) {
            view.data = dataOf(id, *packet);
        }
    }
    const mod::PacketView before = view;
    for (const std::shared_ptr<Entry>& entry : current) {
        if (!entry->handle->active()) {
            continue;
        }
        bool keep = true;
        guarded(errors, entry->owner, [&] { keep = outbound ? entry->typed->outboundTyped(view) : entry->typed->inboundTyped(view); });
        if (!keep) {
            return false;
        }
    }
    if (!packet || view.data.index() != before.data.index() || unchanged(before, view)) {
        return true;
    }
    writeView(view, &before, *packet);
    if (std::optional<std::string> encoded = encodePacket(*codec, *packet)) {
        payload = std::move(*encoded);
    }
    return true;
}

void PacketFilters::remove(const Entry* entry)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    auto same = [entry](const std::shared_ptr<Entry>& other) {
        return other.get() == entry;
    };
    std::erase_if(entries, same);
    std::erase_if(typedEntries, same);
    refresh();
}

void PacketFilters::refresh()
{
    filtering = !entries.empty();
    typedFiltering = !typedEntries.empty();
}

}
