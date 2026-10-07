#pragma once

#include "mod/Canvas.h"
#include "mod/Event.h"

#include <filesystem>
#include <span>

namespace kestrel::mod {

using TextureHandle = uint64_t;

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    // Row-major RGBA8, straight alpha. Exactly width * height * 4 bytes.
    std::vector<uint8_t> pixels;
};

struct TextureInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

namespace detail {
struct TextureRequest : Event {
    KESTREL_EVENT("kestrel:texture_request/v1")
    enum class Action { Supported, Load, Decode, Create, Info, Read, Update, Patch, Draw, Destroy, Clear };
    Action action = Action::Supported;
    TextureHandle handle = 0;
    std::filesystem::path path;
    std::span<const uint8_t> encoded;
    Image image;
    uint32_t x = 0, y = 0;
    Canvas* canvas = nullptr;
    Rect rect;
    Color tint { 255, 255, 255, 255 };
    bool result = false;
};
}

// Optional API 3 extension. Main-thread only; handles belong to this mod and
// are released on unload. Zero/false/empty means unsupported or invalid input.
class Textures {
public:
    explicit Textures(EventBus& events) : events(events) { }
    bool supported() const { return request(detail::TextureRequest::Action::Supported); }
    TextureHandle load(const std::filesystem::path& path) const
    {
        detail::TextureRequest event;
        event.action = detail::TextureRequest::Action::Load;
        event.path = path;
        events.post(event);
        return event.result ? event.handle : 0;
    }
    TextureHandle decode(std::span<const uint8_t> encoded) const
    {
        detail::TextureRequest event;
        event.action = detail::TextureRequest::Action::Decode;
        event.encoded = encoded;
        events.post(event);
        return event.result ? event.handle : 0;
    }
    TextureHandle create(Image image) const
    {
        detail::TextureRequest event;
        event.action = detail::TextureRequest::Action::Create;
        event.image = std::move(image);
        events.post(event);
        return event.result ? event.handle : 0;
    }
    TextureInfo info(TextureHandle handle) const
    {
        detail::TextureRequest event;
        event.action = detail::TextureRequest::Action::Info;
        event.handle = handle;
        events.post(event);
        return { event.image.width, event.image.height, event.result };
    }
    Image read(TextureHandle handle) const
    {
        detail::TextureRequest event;
        event.action = detail::TextureRequest::Action::Read;
        event.handle = handle;
        events.post(event);
        return event.result ? std::move(event.image) : Image {};
    }
    bool update(TextureHandle handle, Image image) const { return write(detail::TextureRequest::Action::Update, handle, std::move(image), 0, 0); }
    bool updateRegion(TextureHandle handle, uint32_t x, uint32_t y, Image image) const { return write(detail::TextureRequest::Action::Patch, handle, std::move(image), x, y); }
    // Draw only inside a HUD/UI render callback. Uses its Canvas clip and GUI scale.
    // Pixel changes appear after the next UI atlas upload, normally the next frame.
    bool draw(Canvas& canvas, TextureHandle handle, Rect rect, Color tint = { 255, 255, 255, 255 }) const
    {
        detail::TextureRequest event;
        event.action = detail::TextureRequest::Action::Draw;
        event.handle = handle;
        event.canvas = &canvas;
        event.rect = rect;
        event.tint = tint;
        events.post(event);
        return event.result;
    }
    bool destroy(TextureHandle handle) const { return request(detail::TextureRequest::Action::Destroy, handle); }
    void clear() const { request(detail::TextureRequest::Action::Clear); }
private:
    bool request(detail::TextureRequest::Action action, TextureHandle handle = 0) const
    {
        detail::TextureRequest event;
        event.action = action;
        event.handle = handle;
        events.post(event);
        return event.result;
    }
    bool write(detail::TextureRequest::Action action, TextureHandle handle, Image image, uint32_t x, uint32_t y) const
    {
        detail::TextureRequest event;
        event.action = action;
        event.handle = handle;
        event.image = std::move(image);
        event.x = x;
        event.y = y;
        events.post(event);
        return event.result;
    }
    EventBus& events;
};

}
