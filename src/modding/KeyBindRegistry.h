#pragma once

#include "ModSupport.h"

#include "mod/Input.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::mod {
class Config;
}

namespace kestrel::modding {

class KeyBindRegistry {
public:
    struct Entry {
        size_t owner = 0;
        std::string modId;
        std::string modName;
        mod::KeyBindSpec spec;
        Key key = Key::None;
        mod::Config* config = nullptr;
        std::shared_ptr<Handle> handle;
    };

    struct Listed {
        std::string id;
        std::string label;
        Key key = Key::None;
    };

    KeyBindRegistry() = default;
    ~KeyBindRegistry();

    KeyBindRegistry(const KeyBindRegistry&) = delete;
    KeyBindRegistry& operator=(const KeyBindRegistry&) = delete;

    mod::Subscription add(size_t owner, std::string modId, std::string modName, mod::KeyBindSpec spec, Key key, mod::Config* config);

    Key current(size_t owner, std::string_view bindId) const;
    bool set(std::string_view id, Key key);
    std::vector<Listed> list() const;
    void release(size_t owner);

private:
    void remove(const Entry* entry);

    std::vector<std::shared_ptr<Entry>> entries;
};

}
