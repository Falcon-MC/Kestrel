#include "mod/Api.h"

using namespace kestrel::mod;

class TexturesMod : public Mod {
public:
    TexturesMod() : Mod({ .id = "textures_example", .name = "Textures example", .version = "1.0.0", .author = "Kestrel", .description = "F9 opens an editable texture and an imported image" }) { }
    void onEnable() override
    {
        Image image { 16, 16, std::vector<uint8_t>(16 * 16 * 4) };
        for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x) {
            size_t offset = (y * 16 + x) * 4;
            image.pixels[offset] = (x + y) % 2 ? 240 : 20;
            image.pixels[offset + 1] = (x + y) % 2 ? 160 : 100;
            image.pixels[offset + 2] = 40;
            image.pixels[offset + 3] = 255;
        }
        texture = textures().create(std::move(image));
        on<KeyPressEvent>([this](KeyPressEvent& event) {
            if (event.key == Key::F9 && ui().open("textures")) event.cancel();
        });
        on<HudRenderEvent>([this](HudRenderEvent& event) {
            textures().draw(event.canvas, texture, { event.canvas.width() - 144, 24, 64, 64 });
            if (imported) textures().draw(event.canvas, imported, { event.canvas.width() - 72, 24, 64, 64 });
        });
        on<UiRenderEvent>([this](UiRenderEvent& event) {
            if (event.id != "textures") return;
            float x = (event.canvas.width() - 320) / 2, y = (event.canvas.height() - 280) / 2;
            event.canvas.fill({ x, y, 320, 280 }, { 55, 55, 55, 255 });
            event.canvas.textCentered("Editable textures", { x, y + 10, 320, 20 }, { 255, 255, 255, 255 });
            textures().draw(event.canvas, texture, { x + 24, y + 40, 128, 128 });
            if (imported) textures().draw(event.canvas, imported, { x + 168, y + 40, 128, 128 });
            if (event.controls.button("paint", "Paint centre", { x + 24, y + 185, 128, 24 })) {
                painted = !painted;
                Image patch { 8, 8, std::vector<uint8_t>(8 * 8 * 4, 255) };
                for (size_t i = 0; i < patch.pixels.size(); i += 4) {
                    patch.pixels[i] = painted ? 220 : 40;
                    patch.pixels[i + 1] = 40;
                    patch.pixels[i + 2] = painted ? 40 : 220;
                }
                textures().updateRegion(texture, 4, 4, std::move(patch));
            }
            if (event.controls.button("load", "Import file", { x + 168, y + 185, 128, 24 })) {
                auto next = textures().load(context().dataDirectory() / filename);
                if (next) { textures().destroy(imported); imported = next; }
                status = next ? "Imported" : "Image unavailable";
            }
            event.controls.textField("filename", { x + 24, y + 220, 272, 24 }, filename, "image.png", 256);
            event.canvas.textCentered(status, { x, y + 250, 320, 20 }, { 255, 255, 255, 255 });
        });
    }
private:
    TextureHandle texture = 0, imported = 0;
    bool painted = false;
    std::string filename = "image.png", status;
};

KESTREL_MOD(TexturesMod)
