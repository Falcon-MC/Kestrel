#include "modding/LocalEffects.h"

#include <cmath>
#include <limits>

namespace kestrel::modding {

namespace {

bool positionValid(const mod::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
        && std::abs(value.x) <= 30000000.0 && std::abs(value.y) <= 30000000.0 && std::abs(value.z) <= 30000000.0;
}

bool volumeValid(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= 4.0f;
}

bool valid(const LocalEffects::Request& request)
{
    using Request = LocalEffects::Request;
    if (request.action == Request::Action::Move) return positionValid(request.position);
    if (request.action == Request::Action::Volume) return request.kind == Request::Kind::Sound && volumeValid(request.volume);
    if (request.action != Request::Action::Create) return true;
    if (request.kind == Request::Kind::Sound) {
        const auto& sound = request.sound;
        return !sound.name.empty() && sound.name.size() <= 256 && volumeValid(sound.volume)
            && std::isfinite(sound.pitch) && sound.pitch >= 0.1f && sound.pitch <= 4.0f
            && (!sound.position || positionValid(*sound.position));
    }
    const auto& particle = request.particle;
    if (particle.identifier.empty() || particle.identifier.size() > 256 || !positionValid(particle.position)
        || !positionValid(particle.direction) || particle.variables.size() > 64) return false;
    for (const auto& [name, value] : particle.variables) {
        if (name.empty() || name.size() > 256 || !std::isfinite(value)) return false;
    }
    return true;
}

}

void LocalEffects::process(size_t owner, Request& request)
{
    request.result = false;
    if (!backend || !valid(request)) return;
    if (request.action == Request::Action::Supported) {
        backend(request);
        return;
    }
    if (request.action == Request::Action::Clear) {
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->second.owner == owner && it->second.kind == request.kind) {
                remove(it->second);
                it = entries.erase(it);
            } else ++it;
        }
        request.result = true;
        return;
    }
    if (request.action == Request::Action::Create) {
        size_t count = 0;
        for (auto it = entries.begin(); it != entries.end();) {
            Request query;
            query.kind = it->second.kind;
            query.action = Request::Action::Active;
            query.handle = it->second.native;
            backend(query);
            if (!query.result) it = entries.erase(it);
            else { count += it->second.owner == owner; ++it; }
        }
        if (count >= 128 || entries.size() >= 1024 || next == std::numeric_limits<uint64_t>::max()) return;
        request.handle = 0;
        backend(request);
        if (!request.result || !request.handle) { request.result = false; request.handle = 0; return; }
        entries.emplace(++next, Entry { owner, request.kind, request.handle });
        request.handle = next;
        return;
    }
    auto found = entries.find(request.handle);
    if (found == entries.end() || found->second.owner != owner || found->second.kind != request.kind) return;
    uint64_t handle = request.handle;
    request.handle = found->second.native;
    backend(request);
    request.handle = handle;
    if (request.action == Request::Action::Remove || (request.action == Request::Action::Active && !request.result)) entries.erase(found);
}

void LocalEffects::remove(const Entry& entry)
{
    Request request;
    request.kind = entry.kind;
    request.action = Request::Action::Remove;
    request.handle = entry.native;
    backend(request);
}

void LocalEffects::release(size_t owner)
{
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->second.owner == owner) {
            remove(it->second);
            it = entries.erase(it);
        } else ++it;
    }
}

void LocalEffects::clear()
{
    for (const auto& [id, entry] : entries) remove(entry);
    entries.clear();
}

}
