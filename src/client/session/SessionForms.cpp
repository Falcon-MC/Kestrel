#include "SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/ClientboundCloseFormPacket.h"
#include "Protocol/Packets/ModalFormRequestPacket.h"
#include "Protocol/Packets/ModalFormResponsePacket.h"

namespace kestrel {

namespace {

constexpr size_t MaxPendingForms = 32;

}

std::vector<FormRequest> Session::takeForms()
{
    std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
    if (!guard.owns_lock()) return {};
    std::vector<FormRequest> forms = std::move(pendingForms);
    pendingForms.clear();
    return forms;
}

void Session::answerForm(uint32_t id, std::optional<std::string> data, bool busy)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (current.state == SessionState::Joined) {
        outgoingForms.push_back({ id, std::move(data), busy });
    }
}

void Session::handleFormPacket(const std::shared_ptr<Packet>& packet)
{
    if (auto request = std::dynamic_pointer_cast<ModalFormRequestPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        if (pendingForms.size() < MaxPendingForms) {
            pendingForms.push_back({ request->mFormId, std::move(request->mFormData), false });
        }
    } else if (std::dynamic_pointer_cast<ClientboundCloseFormPacket>(packet)) {
        std::lock_guard<std::mutex> guard(mutex);
        pendingForms.push_back({ 0, {}, true });
    }
}

void Session::flushForms()
{
    std::vector<FormAnswer> answers;
    {
        std::lock_guard<std::mutex> guard(mutex);
        answers = std::move(outgoingForms);
        outgoingForms.clear();
    }
    for (FormAnswer& answer : answers) {
        ModalFormResponsePacket response;
        response.mFormId = answer.id;
        response.mHasFormData = answer.data.has_value();
        if (answer.data) {
            response.mFormData = std::move(*answer.data);
        } else {
            response.mHasCancelReason = true;
            response.mCancelReason = answer.busy ? ModalFormResponsePacket::CancelReason::UserBusy : ModalFormResponsePacket::CancelReason::UserClosed;
        }
        transmit(response);
    }
}

}
