#pragma once

#include "mod/Canvas.h"
#include "mod/Event.h"
#include "mod/Types.h"

#include <cstdint>
#include <string>

namespace kestrel::mod {

/**
 * Every frame, before anything is drawn.
 */
struct FrameEvent : Event {
    KESTREL_EVENT("kestrel:frame")
    float deltaSeconds = 0.0f;
};

/**
 * Twenty times a second, like the game's own tick, whether or not a world is
 * loaded.
 */
struct TickEvent : Event {
    KESTREL_EVENT("kestrel:tick")
    uint64_t tick = 0;
};

struct ConnectionStateEvent : Event {
    KESTREL_EVENT("kestrel:connection_state")
    ConnectionState previous = ConnectionState::Idle;
    ConnectionState current = ConnectionState::Idle;
};

/**
 * The player spawned in a world: a new server, or the next one after a
 * transfer.
 */
struct JoinEvent : Event {
    KESTREL_EVENT("kestrel:join")
    std::string server;
    std::string address;
};

struct DisconnectEvent : Event {
    KESTREL_EVENT("kestrel:disconnect")
    std::string reason;
};

struct DeathEvent : Event {
    KESTREL_EVENT("kestrel:death")
    std::string message;
};

struct RespawnEvent : Event {
    KESTREL_EVENT("kestrel:respawn")
};

struct DimensionChangeEvent : Event {
    KESTREL_EVENT("kestrel:dimension_change")
    int previous = 0;
    int current = 0;
};

enum class ChatKind {
    Chat,
    Whisper,
    Announcement,
    System,
    Popup,
    Tip,
};

/**
 * A message from the server, already translated and formatted. Changing text
 * changes what is shown; cancelling hides it.
 */
struct ChatReceivedEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:chat_received")
    ChatKind kind = ChatKind::Chat;
    std::string source;
    std::string raw;
    std::string text;
};

/**
 * A line the player typed, before it goes to the server. Mod commands are
 * taken out before this fires.
 */
struct ChatSendEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:chat_send")
    std::string text;
};

struct TitleEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:title")
    bool subtitle = false;
    std::string text;
};

struct ActionbarEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:actionbar")
    std::string text;
};

struct ToastEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:toast")
    std::string title;
    std::string content;
};

/**
 * A server form about to open. A mod that cancels it should answer it with
 * Network::answerForm, or the server keeps waiting.
 */
struct FormEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:form")
    uint32_t id = 0;
    std::string json;
};

/**
 * A key went down. Cancelling keeps the rest of the client from seeing the
 * press; keys held down still count as held.
 */
struct KeyPressEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:key_press")
    Key key = Key::None;
    // True while playing, false in menus, chat and other screens.
    bool inGame = false;
};

enum class MouseButton {
    Left,
    Right,
    Middle,
};

/**
 * A mouse button went down; x and y are in interface units, the ones Canvas
 * draws in. Cancelling keeps the click from the rest of the client.
 */
struct MouseClickEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:mouse_click")
    MouseButton button = MouseButton::Left;
    float x = 0.0f;
    float y = 0.0f;
    bool inGame = false;
};

/**
 * The cursor moved; x and y are where it is now and deltaX and deltaY how far
 * it went since the last move, all in interface units. Sent once per frame
 * at most, only when the cursor position changed.
 */
struct MouseMoveEvent : Event {
    KESTREL_EVENT("kestrel:mouse_move")
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    bool inGame = false;
};

/**
 * The mouse wheel turned; delta is positive rolling up, in notches. x and y
 * are in interface units. Cancelling keeps the scroll from the rest of the
 * client.
 */
struct MouseScrollEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:mouse_scroll")
    float delta = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    bool inGame = false;
};

/**
 * A key went up; the same keys as KeyPressEvent.
 */
struct KeyReleaseEvent : Event {
    KESTREL_EVENT("kestrel:key_release")
    Key key = Key::None;
    bool inGame = false;
};

/**
 * A mouse button went up; x and y are in interface units.
 */
struct MouseReleaseEvent : Event {
    KESTREL_EVENT("kestrel:mouse_release")
    MouseButton button = MouseButton::Left;
    float x = 0.0f;
    float y = 0.0f;
    bool inGame = false;
};

/**
 * A character typed on the keyboard, as its codepoint and its UTF-8 text.
 * Cancelling keeps it out of the client's text fields, for a mod's own one.
 */
struct TextInputEvent : CancellableEvent {
    KESTREL_EVENT("kestrel:text_input")
    char32_t codepoint = 0;
    std::string text;
    bool inGame = false;
};

/**
 * The client settings that SettingsChangedEvent::changed can name, one bit
 * each.
 */
enum class SettingsChange : uint32_t {
    Fov = 1u << 0,
    GuiScale = 1u << 1,
    Language = 1u << 2,
    RenderDistance = 1u << 3,
    MaxFps = 1u << 4,
    Vsync = 1u << 5,
    GameplayFov = 1u << 6,
    Fullscreen = 1u << 7,
    Volumes = 1u << 8,
    KeyBindings = 1u << 9,
    Chat = 1u << 10,
    Brightness = 1u << 11,
    SafeArea = 1u << 12,
    PaperDoll = 1u << 13,
};

/**
 * The player changed client settings and they were just saved. changed holds
 * a SettingsChange bit for each one; the rest are the values now in use.
 */
struct SettingsChangedEvent : Event {
    KESTREL_EVENT("kestrel:settings_changed")
    uint32_t changed = 0;
    int fov = 0;
    float guiScale = 1.0f;
    int renderDistance = 0;
    std::string language;

    bool has(SettingsChange setting) const
    {
        return (changed & static_cast<uint32_t>(setting)) != 0;
    }
};

/**
 * The player asked mods to read their configuration again. modId names the
 * one mod asked, or is empty for all of them.
 */
struct ConfigReloadEvent : Event {
    KESTREL_EVENT("kestrel:config_reload")
    std::string modId;

    bool targets(const std::string& id) const
    {
        return modId.empty() || modId == id;
    }
};

/**
 * The client is about to quit; the last chance to save, sent once before mods
 * are unloaded.
 */
struct ShutdownEvent : Event {
    KESTREL_EVENT("kestrel:shutdown")
};

enum class ScreenKind {
    None,
    Title,
    Play,
    Settings,
    ServerForm,
    Marketplace,
    DressingRoom,
    Profile,
    Pause,
    Chat,
    Death,
    Inventory,
    Container,
    Form,
    Social,
    SignIn,
    Connecting,
    ConnectionError,
    Dialog,
};

/**
 * A screen, dialog or overlay opened. containerType is the client's
 * ContainerType value for Container (2 a chest, 3 a workbench, 4 a furnace
 * and so on), and -1 otherwise. When one replaces another, the close
 * of the old one comes first.
 */
struct ScreenOpenEvent : Event {
    KESTREL_EVENT("kestrel:screen_open")
    ScreenKind screen = ScreenKind::None;
    int containerType = -1;
};

struct ScreenCloseEvent : Event {
    KESTREL_EVENT("kestrel:screen_close")
    ScreenKind screen = ScreenKind::None;
    int containerType = -1;
};

/**
 * An entity appeared around the player. uniqueId is 0 when the server did not
 * give one.
 */
struct EntitySpawnEvent : Event {
    KESTREL_EVENT("kestrel:entity_spawn")
    uint64_t runtimeId = 0;
    int64_t uniqueId = 0;
    std::string identifier;
    std::string name;
    Vec3 position;
    bool isPlayer = false;
};

/**
 * An entity went away, out of range, killed, or because the player left or
 * changed dimension; position is where it was last seen.
 */
struct EntityRemoveEvent : Event {
    KESTREL_EVENT("kestrel:entity_remove")
    uint64_t runtimeId = 0;
    int64_t uniqueId = 0;
    std::string identifier;
    std::string name;
    Vec3 position;
    bool isPlayer = false;
};

enum class InventoryKind {
    Main,
    Armor,
    Offhand,
    Cursor,
};

/**
 * A slot of the local player's inventory holds something else. slot counts
 * within kind: 0 to 35 for Main (0 to 8 the hotbar), 0 to 3 for Armor, and 0
 * for Offhand and Cursor. An empty slot has an empty identifier and count 0.
 */
struct InventoryChangeEvent : Event {
    KESTREL_EVENT("kestrel:inventory_change")
    InventoryKind kind = InventoryKind::Main;
    int slot = 0;
    std::string oldIdentifier;
    int32_t oldCount = 0;
    int32_t oldAux = 0;
    std::string newIdentifier;
    int32_t newCount = 0;
    int32_t newAux = 0;
};

/**
 * The movement the player's keys ask for this frame, before it reaches the
 * physics. forward and sideways run from -1 to 1.
 */
struct MovementEvent : Event {
    KESTREL_EVENT("kestrel:movement")
    float forward = 0.0f;
    float sideways = 0.0f;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;

    /**
     * Set overrideRotation to send the server this rotation for the tick
     * instead of where the player looks; the movement keys follow it too.
     */
    bool overrideRotation = false;
    Rotation rotation;
};

/**
 * Time to draw over the HUD. Only fires while a world is on screen;
 * screenOpen says a menu, chat or inventory covers it.
 */
struct HudRenderEvent : Event {
    KESTREL_EVENT("kestrel:hud_render")

    explicit HudRenderEvent(Canvas& canvas)
        : canvas(canvas)
    {
    }

    Canvas& canvas;
    bool screenOpen = false;
};

/**
 * Time to draw custom shaded geometry into the world, after the game has
 * drawn everything in it.
 */
struct WorldRenderEvent : Event {
    KESTREL_EVENT("kestrel:world_render")

    explicit WorldRenderEvent(WorldPainter& painter)
        : painter(painter)
    {
    }

    WorldPainter& painter;
};

/**
 * Time to add post processing passes for this frame.
 */
struct PostProcessEvent : Event {
    KESTREL_EVENT("kestrel:post_process")

    explicit PostProcessEvent(PostChain& chain)
        : chain(chain)
    {
    }

    PostChain& chain;
};

/**
 * A packet that came in or went out, delivered on the main thread after the
 * fact. To change or drop packets use a PacketFilter.
 */
struct PacketReceivedEvent : Event {
    KESTREL_EVENT("kestrel:packet_received")
    int id = 0;
    std::string name;
    std::string payload;
};

struct PacketSentEvent : Event {
    KESTREL_EVENT("kestrel:packet_sent")
    int id = 0;
    std::string name;
    std::string payload;
};

}
