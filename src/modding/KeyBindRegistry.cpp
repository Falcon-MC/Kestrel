#include "KeyBindRegistry.h"

#include "mod/Config.h"
#include "platform/Keys.h"

namespace kestrel::modding {

KeyBindRegistry::~KeyBindRegistry()
{
    for (const std::shared_ptr<Entry>& entry : entries) {
        entry->handle->expire();
    }
}

mod::Subscription KeyBindRegistry::add(size_t owner, std::string modId, std::string modName, mod::KeyBindSpec spec, Key key, mod::Config* config)
{
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->modId = std::move(modId);
    entry->modName = std::move(modName);
    entry->spec = std::move(spec);
    entry->key = key;
    entry->config = config;
    entry->handle = std::make_shared<Handle>([this, raw = entry.get()] { remove(raw); });
    entries.push_back(entry);
    return mod::Subscription(entry->handle);
}

Key KeyBindRegistry::current(size_t owner, std::string_view bindId) const
{
    for (const std::shared_ptr<Entry>& entry : entries) {
        if (entry->owner == owner && entry->spec.id == bindId) {
            return entry->key;
        }
    }
    return Key::None;
}

bool KeyBindRegistry::set(std::string_view id, Key key)
{
    for (const std::shared_ptr<Entry>& entry : entries) {
        const std::string full = entry->modId + ":" + entry->spec.id;
        if (full != id) {
            continue;
        }
        entry->key = key;
        if (entry->config) {
            entry->config->put("bind." + entry->spec.id, keyName(key));
            entry->config->save();
        }
        return true;
    }
    return false;
}

std::vector<KeyBindRegistry::Listed> KeyBindRegistry::list() const
{
    std::vector<Listed> listed;
    listed.reserve(entries.size());
    for (const std::shared_ptr<Entry>& entry : entries) {
        std::string label = entry->spec.label;
        if (!entry->modName.empty()) {
            label = entry->modName + " — " + label;
        }
        listed.push_back({ entry->modId + ":" + entry->spec.id, std::move(label), entry->key });
    }
    return listed;
}

void KeyBindRegistry::release(size_t owner)
{
    std::erase_if(entries, [owner](const std::shared_ptr<Entry>& entry) {
        if (entry->owner != owner) {
            return false;
        }
        entry->handle->expire();
        return true;
    });
}

void KeyBindRegistry::remove(const Entry* entry)
{
    std::erase_if(entries, [entry](const std::shared_ptr<Entry>& candidate) { return candidate.get() == entry; });
}

}