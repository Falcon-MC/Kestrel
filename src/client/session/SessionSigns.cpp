#include "client/session/SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/BlockActorDataPacket.h"
#include "Protocol/Packets/OpenSignPacket.h"

#include <cmath>
#include <utility>

namespace kestrel {

std::optional<SignEditorRequest> Session::takeSignEditor()
{
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (!guard.owns_lock()) return std::nullopt;
    return std::exchange(pendingSignEditor, std::nullopt);
}

void Session::answerSign(SignEditorAnswer answer)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state == SessionState::Joined && activeSignEditor && activeSignEditor->serial == answer.serial && answer.text.size() <= 4096) {
        outgoingSign = std::move(answer);
    }
}

std::optional<Tag> Session::editableSignAt(const std::array<int32_t, 3>& cell)
{
    const MotionVector& feet = motion.position();
    double dx = double(cell[0]) + 0.5 - feet.x;
    double dy = double(cell[1]) + 0.5 - (feet.y + 1.62);
    double dz = double(cell[2]) + 0.5 - feet.z;
    double distance = dx * dx + dy * dy + dz * dz;
    if (!std::isfinite(distance) || distance > 64.0 || !assets) return std::nullopt;
    std::string name = assets->blockName(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get());
    if (!name.ends_with("_sign")) return std::nullopt;
    auto entities = world.store().blockEntities({ current.dimension, cell[0] >> 4, cell[1] >> 4, cell[2] >> 4 });
    if (!entities) return std::nullopt;
    auto found = entities->find(static_cast<uint16_t>(world::linearIndex(uint32_t(cell[0] & 15), uint32_t(cell[1] & 15), uint32_t(cell[2] & 15))));
    if (found == entities->end()) return std::nullopt;
    const Tag& data = found->second;
    if (!data.isCompound() || (data.getString("id") != "Sign" && data.getString("id") != "HangingSign") || data.getBool("IsWaxed")) return std::nullopt;
    int64_t owner = data.getLong("LockedForEditingBy", -1);
    if (owner != -1 && owner != localUniqueId) return std::nullopt;
    return data;
}

void Session::handleSignPacket(const std::shared_ptr<Packet>& packet)
{
    auto open = std::dynamic_pointer_cast<OpenSignPacket>(packet);
    if (!open) return;
    std::array<int32_t, 3> cell { open->mPosition.x, open->mPosition.y, open->mPosition.z };
    auto data = editableSignAt(cell);
    if (!data) return;
    SignEditorRequest request;
    request.serial = ++signEditorSerial;
    request.cell = cell;
    request.dimension = current.dimension;
    request.front = open->mFrontSide;
    request.hanging = data->getString("id") == "HangingSign";
    request.texture = signEditorTexture(assets->blockName(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get()), request.hanging);
    request.text = signTexts(*data)[request.front ? 0 : 1];
    std::lock_guard<std::mutex> guard(mutex);
    activeSignEditor = request;
    pendingSignEditor = std::move(request);
    outgoingSign.reset();
}

void Session::flushSign()
{
    std::optional<SignEditorRequest> request;
    std::optional<SignEditorAnswer> answer;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!outgoingSign) return;
        answer = std::exchange(outgoingSign, std::nullopt);
        if (!activeSignEditor || activeSignEditor->serial != answer->serial) return;
        request = std::exchange(activeSignEditor, std::nullopt);
    }
    if (request->dimension != current.dimension) return;
    auto data = editableSignAt(request->cell);
    if (!data) return;
    auto edited = editedSignData(std::move(*data), request->front, answer->text);
    if (!edited) return;
    BlockActorDataPacket response;
    response.mBlockPosition = { request->cell[0], request->cell[1], request->cell[2] };
    response.mData = std::move(*edited);
    transmit(response);
}

}
