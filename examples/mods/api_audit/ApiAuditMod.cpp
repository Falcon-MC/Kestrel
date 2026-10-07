#include "mod/Api.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>

using namespace kestrel::mod;

class ApiAuditMod : public Mod {
public:
    ApiAuditMod() : Mod({ .id = "api_audit", .name = "Mod API audit", .version = "1.0.0", .author = "Kestrel", .description = "F10: effects, audio, UI and editable image checks (API 3)" }) { }
    void onEnable() override
    {
        rows.clear();
        check("Optional extensions available", ui().supported() && textures().supported() && particles().supported() && audio().supported());
        resetTexture();
        on<KeyPressEvent>([this](KeyPressEvent& e) {
            if (e.key == Key::F10) { ui().open("audit"); e.cancel(); }
        });
        on<HudRenderEvent>([this](HudRenderEvent& e) {
            textures().draw(e.canvas, texture, { e.canvas.width() - 88, 24, 64, 64 });
            e.canvas.text("API audit - F10", e.canvas.width() - 150, 94, white);
            e.canvas.text(particles().active(particle) ? "Particle: active" : "Particle: stopped", 16, 24, white);
            e.canvas.text(audio().playing(sound) ? "Sound: playing" : "Sound: stopped", 16, 40, white);
        });
        on<UiRenderEvent>([this](UiRenderEvent& e) { render(e); });
    }
private:
    static constexpr Color white { 255, 255, 255, 255 };
    TextureHandle texture = 0, imported = 0;
    ParticleHandle particle = 0;
    SoundHandle sound = 0;
    std::vector<std::pair<std::string, bool>> rows;
    std::string input = "Edit me", filename = "image.png", status = "Ready";
    float volume = 0.25f;
    int page = 0, clicks = 0;
    bool red = false;
    static Image solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
    {
        Image image { w, h, std::vector<uint8_t>(size_t(w) * h * 4, 255) };
        for (size_t i = 0; i < image.pixels.size(); i += 4) {
            image.pixels[i] = r; image.pixels[i + 1] = g; image.pixels[i + 2] = b;
        }
        return image;
    }
    void check(std::string name, bool ok)
    {
        rows.emplace_back(name, ok);
        status = std::string(ok ? "PASS: " : "FAIL: ") + name;
        std::ofstream file(context().dataDirectory() / "results.tsv", std::ios::app);
        file << (ok ? "PASS\t" : "FAIL\t") << name << '\n';
    }
    void resetTexture()
    {
        textures().destroy(texture);
        auto image = solid(16, 16, 20, 100, 40);
        for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x)
            if ((x + y) % 2) { auto i = (y * 16 + x) * 4; image.pixels[i] = 240; image.pixels[i + 1] = 160; }
        texture = textures().create(std::move(image));
    }
    Vec3 front() const
    {
        constexpr double rad = 3.141592653589793 / 180;
        auto p = player().eyePosition(); auto r = player().rotation();
        p.x -= 3 * std::sin(r.yaw * rad) * std::cos(r.pitch * rad);
        p.y -= 3 * std::sin(r.pitch * rad);
        p.z += 3 * std::cos(r.yaw * rad) * std::cos(r.pitch * rad);
        return p;
    }
    void textureChecks()
    {
        auto h = textures().create(solid(4, 4, 10, 20, 30));
        check("Create / info / read", h && textures().info(h).width == 4 && textures().read(h).pixels == solid(4, 4, 10, 20, 30).pixels);
        auto copy = textures().read(h);
        if (!copy.pixels.empty()) copy.pixels[0] = 99;
        auto original = textures().read(h);
        check("Read returns independent pixels", !original.pixels.empty() && original.pixels[0] == 10);
        check("Full update and resize", textures().update(h, solid(8, 2, 40, 50, 60)) && textures().info(h).height == 2);
        check("Region update", textures().updateRegion(h, 2, 1, solid(2, 1, 200, 0, 0)) && textures().read(h).pixels[40] == 200);
        check("Invalid region rejected", !textures().updateRegion(h, 8, 0, solid(1, 1, 0, 0, 0)));
        check("Malformed image rejected", !textures().create({ 2, 2, { 1 } }));
        check("Destroy / stale handle", textures().destroy(h) && !textures().info(h).valid && !textures().update(h, solid(1, 1, 0, 0, 0)));
        check("Missing import rejected", !textures().load(context().dataDirectory() / "missing.png"));
        for (auto name : { "image.png", "image.jpg", "image.tga" }) {
            auto path = context().dataDirectory() / name;
            auto loaded = textures().load(path);
            check(std::string("Import ") + name, loaded && textures().info(loaded).width == 32);
            std::ifstream file(path, std::ios::binary);
            std::vector<uint8_t> bytes { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            auto decoded = textures().decode(bytes);
            check(std::string("Memory decode ") + name, decoded && textures().read(decoded).pixels == textures().read(loaded).pixels);
            textures().destroy(loaded); textures().destroy(decoded);
        }
        check("Malformed encoded image rejected", !textures().decode(std::vector<uint8_t> { 1, 2, 3 }));
    }
    void invalidChecks()
    {
        auto nan = std::numeric_limits<double>::quiet_NaN();
        check("Invalid particle handle rejected", !particles().move(0, front()) && !particles().remove(0));
        check("Invalid audio handle rejected", !audio().stop(0) && !audio().setVolume(0, 1) && !audio().setPosition(0, front()));
        check("Nonfinite particle position rejected", !particles().spawn({ .identifier = "minecraft:evoker_spell", .position = { nan, 0, 0 } }));
        check("Invalid sound volume rejected", !audio().play({ .name = "random.pop", .volume = -1 }));
        check("Invalid sound pitch rejected", !audio().play({ .name = "random.pop", .pitch = 0 }));
        check("Invalid screen ID rejected", !ui().open(""));
        check("Unknown screen is closed", !ui().isOpen("missing") && !ui().close("missing"));
    }
    void render(UiRenderEvent& e)
    {
        auto& c = e.canvas; auto& u = e.controls;
        float x = (c.width() - 580) / 2, y = (c.height() - 420) / 2;
        c.fill({ x, y, 580, 420 }, { 35, 39, 45, 245 });
        c.textCentered("Mod API 3 - live audit", { x, y + 8, 580, 20 }, white);
        if (e.id == "nested") {
            c.textCentered("Nested modal screen - parent retained", { x, y + 90, 580, 30 }, white);
            if (u.button("back", "Back to parent", { x + 180, y + 150, 220, 28 })) {
                check("Close nested / parent retained", ui().close("nested") && ui().isOpen("audit"));
            }
            return;
        }
        if (e.id != "audit") return;
        const char* tabs[] = { "Controls", "Textures", "Effects / audio", "Results" };
        for (int i = 0; i < 4; ++i)
            if (u.button(std::string("tab") + std::to_string(i), tabs[i], { x + 12 + i * 140.0f, y + 36, 136, 26 })) page = i;
        auto button = [&](const char* id, const std::string& label, int col, int row) {
            return u.button(id, label, { x + 20 + col * 270.0f, y + 80 + row * 38.0f, 260, 28 });
        };
        if (page == 0) {
            if (u.textField("text", { x + 20, y + 82, 540, 28 }, input, "UTF-8 text", 128)) status = "Text: " + input;
            if (u.slider("slider", { x + 20, y + 128, 540, 20 }, volume, 0, 4, 0.05f)) {
                if (audio().playing(sound)) check("Live volume", audio().setVolume(sound, volume));
            }
            c.text("Slider: " + std::to_string(volume), x + 20, y + 155, white);
            if (button("count", "Clicks: " + std::to_string(clicks), 0, 3)) { ++clicks; status = "Button activated: " + std::to_string(clicks); }
            u.button("disabled", "Disabled button", { x + 290, y + 194, 260, 28 }, false);
            if (button("focus", "Focus text field", 0, 4)) { u.focus("text"); status = "Focus requested"; }
            if (button("nested", "Open nested screen", 1, 4)) check("Open / isOpen", ui().open("nested") && ui().isOpen("nested"));
            c.text(u.focused("text") ? "Text has focus" : "Text not focused", x + 20, y + 292, white);
            c.text("Tab / Shift+Tab, arrows, Ctrl+A, Enter, Escape", x + 20, y + 322, white);
        } else if (page == 1) {
            textures().draw(c, texture, { x + 30, y + 80, 112, 112 });
            if (imported) {
                textures().draw(c, imported, { x + 170, y + 80, 112, 112 });
                textures().draw(c, imported, { x + 310, y + 80, 112, 112 }, { 255, 100, 100, 180 });
                c.setClip({ x + 450, y + 80, 45, 112 });
                textures().draw(c, imported, { x + 450, y + 80, 112, 112 }); c.clearClip();
            }
            u.textField("filename", { x + 20, y + 205, 540, 26 }, filename, "image.png", 256);
            if (button("paint", "Patch centre", 0, 5)) {
                red = !red; check("Visible centre patch", textures().updateRegion(texture, 4, 4, solid(8, 8, red ? 220 : 40, 40, red ? 40 : 220)));
            }
            if (button("import", "Import file", 1, 5)) {
                auto h = textures().load(context().dataDirectory() / filename);
                if (h) { textures().destroy(imported); imported = h; }
                status = h ? "Imported: " + filename : "Rejected; previous image retained";
            }
            if (button("checks", "Run texture checks", 0, 6)) textureChecks();
            if (button("cleartex", "Clear all textures / recreate", 1, 6)) {
                auto old = texture; textures().clear(); imported = 0;
                check("Clear invalidates handles", !textures().info(old).valid); resetTexture();
            }
        } else if (page == 2) {
            bool spawn = button("spawn", "Spawn particle", 0, 0);
            bool follow = button("follow", "Attach to player", 1, 0);
            if (spawn || follow) {
                particles().remove(particle);
                ParticleOptions options { .identifier = "minecraft:evoker_spell", .position = front() };
                if (follow) options.attachedEntity = player().runtimeId();
                particle = particles().spawn(std::move(options));
                check(follow ? "Attached spawn / active" : "Spawn / active", particle && particles().active(particle));
            }
            if (button("movep", "Move particle", 0, 1)) check("Move particle", particles().move(particle, front()));
            if (button("removep", "Remove particle", 1, 1)) check("Remove / inactive", particles().remove(particle) && !particles().active(particle));
            bool positional = button("pos", "Loop positional sound", 0, 2);
            bool flat = button("flat", "Loop flat sound", 1, 2);
            if (positional || flat) {
                audio().stop(sound);
                SoundOptions options { .name = "random.pop", .volume = volume, .loop = true };
                if (positional) options.position = front();
                sound = audio().play(std::move(options)); check(flat ? "Flat loop / playing" : "Positional loop / playing", sound && audio().playing(sound));
                if (flat && sound) check("Flat sound cannot be repositioned", !audio().setPosition(sound, front()));
            }
            if (button("moves", "Move sound", 0, 3)) check("Move positional sound", audio().setPosition(sound, front()));
            if (button("stops", "Stop sound", 1, 3)) check("Stop / not playing", audio().stop(sound) && !audio().playing(sound));
            if (button("once", "One-shot + expiry check", 0, 4)) {
                auto h = audio().play({ .name = "random.pop", .volume = volume });
                check("One-shot started", h && audio().playing(h));
                if (h) after(3, [this, h] { check("One-shot expired", !audio().playing(h)); });
            }
            if (button("stopall", "Clear effects / stop all", 1, 4)) {
                particles().clear(); audio().stopAll();
                check("Clear particles / stopAll", !particles().active(particle) && !audio().playing(sound));
            }
            if (button("invalid", "Invalid input checks", 0, 5)) invalidChecks();
            c.text(particles().active(particle) ? "Particle active" : "Particle inactive", x + 20, y + 320, white);
            c.text(audio().playing(sound) ? "Sound playing" : "Sound stopped", x + 290, y + 320, white);
        } else {
            int failed = 0; for (const auto& row : rows) if (!row.second) ++failed;
            c.text(std::to_string(rows.size()) + " recorded checks / " + std::to_string(failed) + " failures", x + 20, y + 78, white);
            auto start = rows.size() > 13 ? rows.size() - 13 : 0;
            for (size_t i = start; i < rows.size(); ++i)
                c.text(std::string(rows[i].second ? "PASS " : "FAIL ") + rows[i].first, x + 20, y + 104 + (i - start) * 17, rows[i].second ? Color { 130, 255, 150, 255 } : Color { 255, 100, 100, 255 });
        }
        c.text(status, x + 20, y + 360, white);
        if (u.button("close", "Close - F10 to reopen", { x + 160, y + 386, 260, 24 })) ui().close("audit");
    }
};

KESTREL_MOD(ApiAuditMod)
