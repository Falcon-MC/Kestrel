#pragma once

#include "mod/Canvas.h"
#include "mod/Event.h"

namespace kestrel::mod {

// Main-thread only. Coordinates use Canvas units; IDs must be stable within a screen.
class Controls {
public:
    virtual ~Controls() = default;
    virtual bool button(std::string_view id, std::string_view label, Rect rect, bool enabled = true) = 0;
    virtual bool slider(std::string_view id, Rect rect, float& value, float minimum, float maximum, float step = 0.0f, bool enabled = true) = 0;
    // UTF-8, single line. Returns true when edited. maxBytes includes no terminator.
    virtual bool textField(std::string_view id, Rect rect, std::string& value, std::string_view placeholder = {}, size_t maxBytes = 1024, bool enabled = true) = 0;
    virtual void focus(std::string_view id) = 0;
    virtual bool focused(std::string_view id) const = 0;
};

// Delivered only to the owner of the topmost screen. References last for this callback.
struct UiRenderEvent : Event {
    KESTREL_EVENT("kestrel:ui_render/v1")
    UiRenderEvent(std::string id, Canvas& canvas, Controls& controls)
        : id(std::move(id)), canvas(canvas), controls(controls) { }
    std::string id;
    Canvas& canvas;
    Controls& controls;
};

namespace detail {
struct UiRequest : Event {
    KESTREL_EVENT("kestrel:ui_request/v1")
    enum class Action { Supported, Open, Close, IsOpen };
    Action action = Action::Supported;
    std::string id;
    bool result = false;
};
}

// Optional API 3 extension. Older hosts report unsupported and safely ignore requests.
// Screens are modal, Escape closes the top screen, and unloading closes the owner's screens.
class Ui {
public:
    explicit Ui(EventBus& events) : events(events) { }
    bool supported() const { return request(detail::UiRequest::Action::Supported, {}); }
    bool open(std::string_view id) const { return request(detail::UiRequest::Action::Open, id); }
    bool close(std::string_view id) const { return request(detail::UiRequest::Action::Close, id); }
    bool isOpen(std::string_view id) const { return request(detail::UiRequest::Action::IsOpen, id); }
private:
    bool request(detail::UiRequest::Action action, std::string_view id) const
    {
        detail::UiRequest event;
        event.action = action;
        event.id = id;
        events.post(event);
        return event.result;
    }
    EventBus& events;
};

}
