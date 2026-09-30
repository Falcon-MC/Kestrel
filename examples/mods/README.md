# Writing mods

A mod is a shared library (`.so`, `.dll` or `.dylib`) in the `mods` folder of Kestrel's data directory
(`~/.local/share/Kestrel/mods` on Linux, `%APPDATA%\Kestrel\mods` on Windows). Everything in the folder loads
at start, in file name order, and stays enabled until Kestrel quits. The folder is created on first run.

Mods talk to Kestrel through C++ interfaces, so they must be built with the same compiler family and standard
library as Kestrel (GCC or Clang with libstdc++ on Linux, MSVC on Windows, Apple Clang on macOS). A mod built
otherwise is refused with a message in `debug.txt` instead of crashing. Only include headers from `mod/`; a mod
never links against Kestrel.

## Building

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyMod CXX)
include(path/to/Kestrel/cmake/KestrelMod.cmake)
kestrel_add_mod(my_mod src/MyMod.cpp)
```

Inside the Kestrel tree, `cmake -DKESTREL_BUILD_EXAMPLE_MODS=ON` builds the examples here.

## The mod class

```cpp
class MyMod : public kestrel::mod::Mod {
public:
    MyMod() : Mod({ .id = "my_mod", .name = "My Mod", .version = "1.0.0", .author = "me" }) { }
    void onEnable() override;   // register everything here
    void onDisable() override;  // optional, for the mod's own resources
};
KESTREL_MOD(MyMod)
```

The id names the mod's data folder (`mods/<id>/`) and may only use `a-z 0-9 . - _`. The context is not
there in the constructor yet, so register things in `onEnable`. Whatever a mod registers (listeners,
commands, key bindings, tasks, packet filters, shaders) is removed when it unloads; keep the returned
`Subscription` only to stop something earlier with `cancel()`.

An exception thrown by mod code is caught, written to `debug.txt` and shown once in chat. It never takes
Kestrel down, though the handler that threw stops for that call.

## Services

| Call | What it does |
| --- | --- |
| `events()` | subscribe to any event by type, post your own events to other mods |
| `chat()` | send to the server, print locally, toast, title, action bar, chat history |
| `player()` | position, rotation (and turning), health, food, xp, inventory, effects, attack, use, drop, respawn |
| `world()` | server, dimension, time, weather, entities, the targeted block, player list, sidebar, any block of a loaded chunk (`block`, `isLoaded`), `raycast` through blocks and entities, `setBlockHidden` to draw blocks as air |
| `network()` | connect, disconnect, raw packets, form answers, packet filters |
| `input()` | held keys, mouse (in Canvas units), `setCursorFree(true)` to show the cursor and pause play while a mod menu is open, whether the player is in game, key bindings (`bind(key)` hidden, `bind({ id, label, defaultKey })` listed in Keyboard & Mouse while the mod is loaded) |
| `commands()` | `.name args` chat commands that never reach the server; `.help` and `.mods` are built in |
| `config()` | `key=value` settings in `mods/<id>/config.txt`, saved on unload |
| `scheduler()` | `after`, `every`, `nextFrame` on the main thread, `post` from any thread |
| `shaders()` | custom shaders for the HUD and the world |
| `camera()` | where the view is drawn from, `detach(position, rotation)` for a free camera, `setFovScale` to zoom |
| `log()` | lines in `debug.txt` and the console, prefixed with the mod id |

Helpers on `Mod` shorten the common cases: `on<Event>(lambda)`, `on(&MyMod::method)`, `command(...)`,
`bind(key, action)`, `bind({ id, label, key }, action)`, `every(seconds, task)` and `after(seconds, task)`.

## Events

All events fire on the main thread. Cancellable ones hide or stop what they describe; handlers run from
`Priority::Lowest` to `Priority::Highest`, then `Monitor`, and skip cancelled events unless they ask for them
with `ListenOptions::receiveCancelled`.

| Event | When | Cancel |
| --- | --- | --- |
| `FrameEvent` | every frame | |
| `TickEvent` | 20 times a second | |
| `ConnectionStateEvent`, `JoinEvent`, `DisconnectEvent` | connection changes | |
| `DeathEvent`, `RespawnEvent`, `DimensionChangeEvent` | player changes | |
| `ChatReceivedEvent` | a server message, `text` can be rewritten | hides it |
| `ChatSendEvent` | a line the player typed, `text` can be rewritten | not sent |
| `TitleEvent`, `ActionbarEvent`, `ToastEvent` | HUD text from the server | not shown |
| `FormEvent` | a server form | not opened, answer it yourself |
| `KeyPressEvent`, `MouseClickEvent` | input | the client ignores the press |
| `MovementEvent` | movement keys each frame, all fields writable; `overrideRotation` sends another rotation | |
| `HudRenderEvent` | draw over the HUD with `event.canvas` | |
| `PostProcessEvent` | add full screen passes with `event.chain` | |
| `WorldRenderEvent` | draw into the world with `event.painter` | |
| `PacketReceivedEvent`, `PacketSentEvent` | copies of packets, after the fact | |

A mod's own events work the same way:

```cpp
struct ScoreEvent : kestrel::mod::Event {
    KESTREL_EVENT("my_mod:score")
    int points = 0;
};
```

## Packet filters

A `PacketFilter` sees each raw game packet, header included, on the network thread and can rewrite it or
drop it by returning false. Keep it quick, and use `scheduler().post(...)` to act on what it saw.

## Custom shaders

A `ShaderSource` carries the shader for each backend: SPIR-V for Vulkan (Linux), HLSL for Direct3D 12
(Windows), Metal Shading Language for macOS. Only the running backend's code is needed and
`shaders().backend()` says which one that is. The entry points are `vs_main` and `ps_main` (`main` in GLSL).

Vertices are a `float3` position, a `float2` uv and a `unorm4` color. The 32 constants (push constants,
`cbuffer b0`, Metal `buffer(1)`) are a column major `float4x4 transform`, `float4 timing` with the seconds
since start and the frame size in pixels, and `float4 params[3]` from the draw's `ShaderParams`. Clip space is
y up, so GLSL flips it the way Kestrel's own shaders do.

```cpp
auto glow = shaders().create(source);           // check glow->valid() and glow->error()
canvas.shader(*glow, rect, { 1.0f, 0.5f, 0.0f }); // HUD, under or over it with aboveHud
painter.box(*glow, min, max);                    // world, depth tested
```

`kestrel_mod_spirv(target shader.vert ...)` compiles GLSL with `glslc` from the Vulkan SDK into
`shader.vert.spv.inc`, which goes straight into a `std::vector<uint32_t>` initializer. `shader_demo` shows
all three backends.

## Post processing

`shaders().createPost(source)` builds a full screen pass for `PostProcessEvent`, which fires after the world is
drawn. Each pass reads the frame as the previous pass left it, the depth buffer, the frame before the first pass
and whatever an earlier pass kept with `keepInput`, and replaces the frame with its output. The transform
constants hold the inverse view projection, so a pass can rebuild world positions from depth;
`world().environment()` has the sun, fog, weather and camera to go with them. Only Vulkan copies the frame for
now; elsewhere `event.chain.supported()` is false and passes are skipped.

## Threads

Use the context from the main thread only. The exceptions are packet filters, which run on the network
thread, and `Scheduler::post`, which any thread may call.
