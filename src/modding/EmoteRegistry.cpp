#include "modding/EmoteRegistry.h"

#include "Core/Json/Json.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace kestrel::modding {

namespace {

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}

EmoteRegistry::~EmoteRegistry()
{
    for (const std::shared_ptr<Entry>& entry : entries) {
        entry->handle->expire();
    }
}

mod::Subscription EmoteRegistry::add(size_t owner, mod::EmoteSpec spec, const ErrorSink& errors)
{
    if (spec.id.empty()) {
        errors(owner, "an emote needs an id");
        return {};
    }
    if (find(spec.id)) {
        errors(owner, "the emote " + spec.id + " is already added");
        return {};
    }
    std::unique_ptr<json::Value> document = json::parse(spec.animation);
    const json::Value* animations = document && document->isObject() ? document->get("animations") : nullptr;
    if (!animations || !animations->isObject() || animations->mKeys.empty()) {
        errors(owner, "the emote " + spec.id + " has no animations in its animation file");
        return {};
    }
    std::string clip = spec.clip.empty() ? animations->mKeys.front() : spec.clip;
    if (!animations->get(clip)) {
        errors(owner, "the emote " + spec.id + " names the animation " + clip + ", which its file lacks");
        return {};
    }
    if (spec.name.empty()) {
        spec.name = spec.id;
    }
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->spec = std::move(spec);
    entry->clip = lowercase(clip);
    entry->handle = std::make_shared<Handle>([this, raw = entry.get()] { remove(raw); });
    entries.push_back(entry);
    ++changes;
    return mod::Subscription(entry->handle);
}

void EmoteRegistry::release(size_t owner)
{
    size_t before = entries.size();
    std::erase_if(entries, [owner](const std::shared_ptr<Entry>& entry) {
        if (entry->owner != owner) {
            return false;
        }
        entry->handle->expire();
        return true;
    });
    if (entries.size() != before) {
        ++changes;
    }
}

std::vector<std::shared_ptr<const EmoteRegistry::Entry>> EmoteRegistry::list() const
{
    return { entries.begin(), entries.end() };
}

const EmoteRegistry::Entry* EmoteRegistry::find(const std::string& id) const
{
    for (const std::shared_ptr<Entry>& entry : entries) {
        if (entry->spec.id == id) {
            return entry.get();
        }
    }
    return nullptr;
}

void EmoteRegistry::requestPlay(std::string id)
{
    request = std::move(id);
}

void EmoteRegistry::requestStop()
{
    request = std::string();
}

std::optional<std::string> EmoteRegistry::takeRequest()
{
    return std::exchange(request, std::nullopt);
}

void EmoteRegistry::remove(const Entry* entry)
{
    size_t before = entries.size();
    std::erase_if(entries, [entry](const std::shared_ptr<Entry>& other) { return other.get() == entry; });
    if (entries.size() != before) {
        ++changes;
    }
}

}
