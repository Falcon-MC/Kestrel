#pragma once

#include "mod/Textures.h"

#include <functional>
#include <map>

namespace kestrel::modding {

class TextureStore {
public:
    // Null image removes the sprite. Non-null images are copied by the backend.
    using Backend = std::function<void(const std::string&, const mod::Image*)>;
    explicit TextureStore(Backend backend) : backend(std::move(backend)) { }
    ~TextureStore();
    void process(size_t owner, mod::detail::TextureRequest& request);
    void release(size_t owner);
private:
    struct Entry { size_t owner; std::string name; mod::Image image; };
    bool capacity(size_t owner, size_t bytes, size_t replaced = 0, bool adding = true) const;
    void remove(std::map<mod::TextureHandle, Entry>::iterator entry);
    Backend backend;
    std::map<mod::TextureHandle, Entry> entries;
    mod::TextureHandle next = 0;
};

}
