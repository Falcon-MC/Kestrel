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
