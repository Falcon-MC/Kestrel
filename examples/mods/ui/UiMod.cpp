#include "mod/Api.h"

using namespace kestrel::mod;

class UiMod : public Mod {
public:
    UiMod() : Mod({ .id = "ui_example", .name = "UI example", .version = "1.0.0", .author = "Kestrel", .description = "F8 opens a screen with native controls" }) { }
    void onEnable() override
    {
        on<KeyPressEvent>([this](KeyPressEvent& event) {
            if (event.key == Key::F8 && ui().supported()) {
                ui().isOpen("demo") ? ui().close("demo") : ui().open("demo");
                event.cancel();
            }
        });
        on<UiRenderEvent>([this](UiRenderEvent& event) {
            if (event.id != "demo") return;
            float x = (event.canvas.width() - 280) / 2, y = (event.canvas.height() - 220) / 2;
            event.canvas.fill({ x, y, 280, 220 }, { 55, 55, 55, 255 });
            event.canvas.textCentered("Mod UI", { x, y + 10, 280, 20 }, { 255, 255, 255, 255 });
            event.controls.textField("name", { x + 20, y + 45, 240, 26 }, name, "Your name", 64);
            event.controls.slider("volume", { x + 20, y + 85, 240, 18 }, volume, 0, 100, 1);
            event.canvas.textCentered("Value: " + std::to_string(int(volume)), { x + 20, y + 110, 240, 20 }, { 255, 255, 255, 255 });
            if (event.controls.button("count", "Clicks: " + std::to_string(clicks), { x + 20, y + 140, 115, 24 })) ++clicks;
            if (event.controls.button("close", "Close", { x + 145, y + 140, 115, 24 })) ui().close("demo");
            event.canvas.textCentered("Tab / Shift+Tab - Escape to close", { x, y + 185, 280, 20 }, { 210, 210, 210, 255 });
        });
    }
private:
    std::string name;
    float volume = 50;
    int clicks = 0;
};

KESTREL_MOD(UiMod)
