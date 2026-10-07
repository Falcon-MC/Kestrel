#include "modding/ModUi.h"
#include "modding/EventDispatcher.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace kestrel;
using namespace kestrel::modding;
using Request = mod::detail::UiRequest;

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

struct Bus : mod::EventBus {
    ModUi* ui = nullptr;
    size_t owner = 1;
    mod::Subscription subscribe(std::string_view, Handler, mod::ListenOptions) override { return {}; }
    void post(mod::Event& event) override
    {
        if (ui && event.type() == Request::Type) ui->process(owner, static_cast<Request&>(event));
    }
};

int main()
{
    EventDispatcher dispatcher([](size_t, std::string_view) { });
    int first = 0, second = 0;
    auto listener1 = dispatcher.subscribe(1, Request::Type, [&](mod::Event&) { ++first; }, {});
    auto listener2 = dispatcher.subscribe(2, Request::Type, [&](mod::Event&) { ++second; }, {});
    Request render;
    dispatcher.dispatchTo(1, render);
    check(first == 1 && second == 0, "render events reach only screen owner");
    dispatcher.release(1);
    dispatcher.dispatchTo(1, render);
    check(first == 1, "unloaded owner receives no callbacks");
    Bus bus;
    mod::Ui api(bus);
    check(!api.supported() && !api.open("test"), "old host fallback");
    ModUi ui;
    bus.ui = &ui;
    check(api.supported() && api.open("test"), "open UI extension");
    check(!api.open(""), "reject empty IDs");
    bus.owner = 2;
    check(!api.close("test") && !api.isOpen("test"), "owner isolation");
    check(api.open("test") && ui.top()->owner == 2, "same ID is owner scoped");
    InputState input;
    input.mouseDown = true;
    input.text = U"secret";
    input.setKey(Key::W, true);
    ui.capture(input, 2);
    check(!input.mouseDown && input.text.empty() && !input.isHeld(Key::W), "modal input capture");
    ui.restoreInput(input);
    check(input.mouseDown && input.isHeld(Key::W), "persistent states survive between platform frames");
    input = {};
    input.escape = true;
    ui.capture(input, 2);
    check(ui.top()->owner == 1 && !input.escape, "Escape pops only top and is consumed");
    ui.release(1);
    check(!ui.open(), "unload closes screens");

    bus.owner = 3;
    for (int i = 0; i < 8; ++i) check(api.open(std::to_string(i)), "owner screen limit accepts eight");
    check(!api.open("overflow"), "owner screen limit rejects ninth");
    auto retained = ui.top();
    ui.release(3);
    check(!ui.open() && retained->id == "7", "render state survives reentrant close");

    UiControlState state;
    mod::Rect rect { 10, 10, 100, 20 };
    auto layout = [&] {
        state.add("text", UiControlState::Kind::Text, rect, true);
        state.add("disabled", UiControlState::Kind::Button, { 10, 40, 100, 20 }, false);
        state.add("slider", UiControlState::Kind::Slider, { 10, 70, 100, 20 }, true);
        state.add("button", UiControlState::Kind::Button, { 10, 100, 100, 20 }, true);
    };
    state.begin({}, 1); layout(); state.end();
    input = {}; input.tab = true;
    state.begin(input, 1); layout(); check(state.focus == "text", "Tab selects first control"); state.end();
    state.begin(input, 1); layout(); check(state.focus == "slider", "Tab skips disabled controls"); state.end();
    input.setKey(Key::Shift, true);
    state.begin(input, 1); layout(); check(state.focus == "text", "Shift Tab reverses"); state.end();

    std::string value = "a";
    input = {}; input.text = U"\u00e9\U0001f600";
    state.begin(input, 1); layout();
    check(state.textField("text", value, 7) && value.size() == 7, "Unicode byte limit"); state.end();
    input = {}; input.backspace = true;
    state.begin(input, 1); layout();
    check(state.textField("text", value, 7) && value.size() == 3, "Unicode backspace"); state.end();
    input = {}; input.pressedKey = Key::Left;
    state.begin(input, 1); layout(); state.textField("text", value, 7); state.end();
    input = {}; input.text = U"b";
    state.begin(input, 1); layout(); state.textField("text", value, 7);
    check(value == "ab\xc3\xa9", "caret insertion"); state.end();
    input = {}; input.setKey(Key::Control, true); input.pressedKey = Key::A;
    state.begin(input, 1); layout(); state.textField("text", value, 7); state.end();
    input = {}; input.text = U"x";
    state.begin(input, 1); layout(); state.textField("text", value, 7);
    check(value == "x", "select all replacement"); state.end();

    input = {}; input.mouseX = 30; input.mouseY = 150; input.mousePressed = input.mouseDown = true;
    state.begin(input, 2); layout();
    float number = 0;
    check(state.slider("slider", { 10, 70, 100, 20 }, number, 0, 100, 1) && number == 5, "GUI scale slider"); state.end();
    input = {}; input.mouseX = 999; input.mouseY = 999; input.mouseDown = true;
    state.begin(input, 2); layout();
    check(state.slider("slider", { 10, 70, 100, 20 }, number, 0, 100, 1) && number == 100, "drag beyond bounds"); state.end();
    input = {}; input.pressedKey = Key::Left;
    state.begin(input, 1); layout();
    check(state.slider("slider", rect, number, 0, 100, 5) && number == 95, "keyboard slider step");
    check(!state.slider("slider", rect, number, 0, 100, -1), "reject invalid steps"); state.end();
    input = {}; input.mousePressed = true; input.mouseX = 900; input.mouseY = 900;
    state.begin(input, 1); layout(); check(state.focus.empty(), "outside click clears focus"); state.end();
    input = {}; input.mouseX = 20; input.mouseY = 110; input.mousePressed = input.mouseDown = true;
    state.begin(input, 1); layout(); check(!state.button("button", { 10, 100, 100, 20 }), "button does not fire on press"); state.end();
    input = {}; input.mouseX = 900; input.mouseY = 900; input.mouseReleased = true;
    state.begin(input, 1); layout(); check(!state.button("button", { 10, 100, 100, 20 }), "release outside cancels activation"); state.end();
    input = {}; input.enter = true;
    state.begin(input, 1); layout(); check(state.button("button", { 10, 100, 100, 20 }), "Enter activates focused button"); state.end();
    state.focus = "text";
    state.begin({}, 1); state.add("text", UiControlState::Kind::Text, rect, false);
    check(state.focus.empty(), "disabled control loses focus"); state.end();
    state.focus = "text";
    state.begin({}, 1); state.end(); check(state.focus.empty(), "removed control loses focus");
    state.begin({}, 1);
    check(!state.add("bad", UiControlState::Kind::Button, { std::numeric_limits<float>::quiet_NaN(), 0, 10, 10 }, true), "reject nonfinite bounds");
    check(state.add("same", UiControlState::Kind::Button, rect, true) && !state.add("same", UiControlState::Kind::Button, rect, true), "reject duplicate IDs");
}
