#include "mod/Api.h"

#include <cmath>
#include <cstdio>

using namespace kestrel::mod;

/**
 * A tour of the API: a HUD readout, a key binding, commands, chat filtering,
 * a setting that survives restarts and a movement tweak.
 */
class HelloMod : public Mod {
public:
    HelloMod()
        : Mod({ .id = "hello", .name = "Hello", .version = "1.0.0", .author = "Kestrel", .description = "Shows what a mod can do" })
    {
    }

    void onEnable() override
    {
        showCoordinates = config().getBool("showCoordinates", true);
        autoSprint = config().getBool("autoSprint", false);

        on(&HelloMod::drawHud);
        on(&HelloMod::chatReceived);
        on<JoinEvent>([this](JoinEvent& event) { chat().toast("Hello", "Joined " + event.server); });
        on<MovementEvent>([this](MovementEvent& event) {
            if (autoSprint && event.forward > 0.0f) {
                event.sprint = true;
            }
        });

        bind(Key::H, [this] {
            showCoordinates = !showCoordinates;
            config().set("showCoordinates", showCoordinates);
        });

        command({ "sprint", "Toggles sprinting whenever you walk forward", "", { "as" } }, [this](CommandContext& context) {
            autoSprint = !autoSprint;
            config().set("autoSprint", autoSprint);
            context.reply(autoSprint ? "§aAuto sprint on" : "§cAuto sprint off");
        });
        command({ "look", "Turns the camera", "<yaw> <pitch>", {} }, [this](CommandContext& context) {
            player().setRotation({ static_cast<float>(context.numberArg(0)), static_cast<float>(context.numberArg(1)) });
        });
        command("nearest", "Names the closest entity", [this](CommandContext& context) {
            std::optional<Entity> found = world().nearestEntity(player().position(), 64.0);
            context.reply(found ? found->identifier + (found->name.empty() ? "" : " (" + found->name + ")") : "Nothing within 64 blocks");
        });

        log().info("ready");
    }

private:
    void drawHud(HudRenderEvent& event)
    {
        if (!showCoordinates || event.screenOpen || !player().inWorld()) {
            return;
        }
        Vec3 position = player().position();
        char line[96];
        std::snprintf(line, sizeof(line), "XYZ %.1f / %.1f / %.1f", position.x, position.y, position.z);

        Canvas& canvas = event.canvas;
        float width = canvas.measure(line) + 8.0f;
        canvas.fill({ 4.0f, 4.0f, width, 14.0f }, { 0, 0, 0, 120 });
        canvas.text(line, 8.0f, 7.0f, { 255, 255, 255, 255 });
    }

    void chatReceived(ChatReceivedEvent& event)
    {
        // Servers love to spam this one.
        if (event.kind == ChatKind::Chat && event.text.find("[AD]") != std::string::npos) {
            event.cancel();
        }
    }

    bool showCoordinates = true;
    bool autoSprint = false;
};

KESTREL_MOD(HelloMod)
