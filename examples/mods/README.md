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

`particles()` and `audio()` return the mod's particle and sound services.
Failed operations return false or handle 0.

```cpp
auto effect = particles().spawn({
    .identifier = "minecraft:basic_flame_particle",
    .position = player().eyePosition(),
    .attachedEntity = player().runtimeId()
});
bool alive = particles().active(effect);
particles().move(effect, player().eyePosition()); // detaches from the entity
particles().remove(effect);                     // removes children too

auto sound = audio().play({
    .name = "random.click",
    .position = player().eyePosition(), // omit for a flat interface sound
    .volume = 0.5f,
    .loop = true
});
audio().setVolume(sound, 0.25f);
audio().setPosition(sound, player().eyePosition());
audio().stop(sound);
```

Effects use definitions from the loaded resource packs. All calls run on the
main thread; use `scheduler().post(...)` from a worker. Each mod controls only
its own handles, with at most 128 active handles per mod and 1024 across mods.
The engines can evict effects earlier when their own budgets are reached.
Handles expire naturally and are invalidated on unload or world changes;
volume/category settings still apply. `particles().clear()` and
`audio().stopAll()` affect only the calling mod. Particle movement updates
emitters; already emitted world-space particles retain their simulated position.

The `effects_example` mod provides `.effects particle <id>`, `.effects follow <id>`,
`.effects sound <name>` (looping), `.effects once <name>`,
`.effects flat <name>`, `.effects move`, `.effects volume <0..4>`,
`.effects status`, and `.effects stop` to exercise the extension.

| Call | What it does |
| --- | --- |
| `events()` | subscribe to any event by type, post your own events to other mods |
| `chat()` | send to the server, print locally, toast, title, action bar, chat history |
| `player()` | position, rotation (and turning), health, food, xp, inventory, effects, `maxDurability` of an item, attack, use, drop, respawn |
| `world()` | server, the address it resolved to (`serverEndpoint`), dimension, time, weather, boss bars, entities, the targeted block, player list, sidebar, any block of a loaded chunk (`block`, `isLoaded`), `raycast` through blocks and entities, `setBlockHidden` to draw blocks as air |
| `network()` | connect, disconnect, raw packets, form answers, packet filters |
| `input()` | held keys, mouse (in Canvas units), `setCursorFree(true)` to show the cursor and pause play while a mod menu is open, whether the player is in game, key bindings (`bind(key)` hidden, `bind({ id, label, defaultKey })` listed in Keyboard & Mouse while the mod is loaded) |
| `commands()` | `.name args` chat commands that never reach the server; `.help` and `.mods` are built in |
| `config()` | `key=value` settings in `mods/<id>/config.txt`, saved on unload |
| `scheduler()` | `after`, `every`, `nextFrame` on the main thread, `post` from any thread |
| `shaders()` | custom shaders for the HUD and the world |
| `camera()` | where the view is drawn from, `detach(position, rotation)` for a free camera, `setFovScale` to zoom, `fieldOfView` |
| `hud()` | `setHidden(element, true)` takes the game's crosshair, sidebar, hotbar, hearts, boss bars, player list and other HUD parts off screen, to draw your own in their place |
| `visuals()` | client side looks: time and weather, fog, brightness, hurt camera, hit color, glint, item physics, swing speed, held item position, name tags, interface scale |
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

## Native mod screens

`ui_example` opens with F8, including on the title screen. It demonstrates a UTF-8
text field, a slider, buttons, and keyboard focus. Escape closes the top screen.

`context().ui()` (or `ui()` in a `Mod`) provides `supported()`, `open(id)`,
`close(id)` and `isOpen(id)`. Listen for `UiRenderEvent` to draw the active screen.
Its `canvas` uses GUI coordinates and its `controls` provides:

- `button(id, label, rect, enabled)` returns true on activation.
- `slider(id, rect, value, minimum, maximum, step, enabled)` returns true on change.
  Step zero allows continuous dragging; arrow keys change by one percent.
- `textField(id, rect, value, placeholder, maxBytes, enabled)` edits a single-line
  UTF-8 string, returning true on change. Left/right move the caret, Backspace
  erases a codepoint, and Ctrl+A selects all. Clicking focuses at the end.
- `focus(id)` and `focused(id)` manage focus explicitly. Tab and Shift+Tab cycle
  through enabled controls in their declaration order.

Declare controls each frame with stable, unique IDs. Only the topmost screen's
owner receives its render event. Screens capture keyboard and pointer input,
release the cursor, and close automatically when their owner unloads. Hidden or
disabled controls lose focus. Screen references are valid only during the event;
all calls must run on the main thread. Use Canvas directly for labels and art.

Each mod may open eight screens, with 32 total, 256 controls per screen and IDs up to 256 bytes.
Text fields accept at most 65,536 bytes. Clipboard, multiline editing and IME
composition are not provided by this extension.

## Images and editable textures

`context().textures()` (or `textures()` in a Mod) returns the mod's texture service.

- `load(path)` imports a PNG, JPEG or TGA file. Use `dataDirectory() / filename`
  for a mod's own files. Relative paths otherwise use the client's working directory.
- `decode(bytes)` imports encoded image bytes in memory.
- `create(Image)` creates a texture from row-major RGBA8 pixels with straight alpha.
- `info(handle)` reads dimensions and validity without copying pixels.
- `read(handle)` returns a pixel copy; edit it and call `update(handle, image)`
  to replace or resize the texture. `updateRegion(handle, x, y, image)` patches
  a rectangle without changing the remaining pixels.
- `draw(canvas, handle, rect, tint)` draws into a HUD or mod-screen render callback,
  respecting the Canvas clip and GUI scale. Tint multiplies RGBA, including alpha.
- `destroy(handle)` and `clear()` release textures. Unloading releases them automatically.

All calls run on the main thread. Handles are owner scoped; zero, false or an empty
image indicate failure. Invalid updates leave the old image intact. Images are limited
to 1024 x 1024, encoded files to 16 MiB, 32 handles / 8 MiB RGBA per mod and 128 handles /
16 MiB RGBA overall. Import is synchronous: load once rather than every frame.
Textures share the existing nearest-filtered UI atlas across all render backends.
Atlas pressure can reduce their resolution. Pixel changes become visible at the next
atlas upload, normally the next frame. No block/entity texture overrides are provided.

`textures_example` opens with F9 and demonstrates procedural pixels, region updates
and file import; it also draws the images in the HUD. Place your own image at `mods/textures_example/image.png` next to
settings.txt, open the screen and click **Import file**. **Paint centre** changes
an 8 x 8 region of the procedural image without replacing its border.

`api_audit` opens with F10, including on the title screen. Its four tabs exercise
native controls and focus, imported/editable textures, particles, and audio.
Checks append PASS/FAIL rows to `mods/api_audit/results.tsv` next to settings.txt.
For the image import checks, place synthetic 32 x 32 fixtures named `image.png`,
`image.jpg`, and `image.tga` in that directory. No game assets are bundled.
Particle checks require a joined world and the vanilla particle pack; audio checks
use `random.pop`. The live volume slider is on Controls. Sound state checks do not
verify perceived volume or spatialization; listen while moving around the source.
Use Escape to close a screen and Clear effects / stop all to clean up explicitly.
Disabling the mod also releases its resources. This example uses API 3.

## API 4

| Area | Added |
| --- | --- |
| `world()` | `collision` and `outline` boxes of a block, `properties` (solid, full cube, liquid, hazard, hardness, friction...), `findBlocksMatching` by properties, `breakTicks` for the held item |
| `player()` | `tickPosition`, `velocity`, `boundingBox`, `fallDistance`, `inWater`, `inLava`, `gliding`, `onClimbable`, collisions, `abilities`; `clickSlot`, `moveItem`, `swapHotbar`, `findItem`, `bestToolFor`; `openContainer`, `openContainerContents`, `closeContainer`; `startBreaking`, `stopBreaking`, `breakingTarget`, `breakingProgress`; `useOn`, `useItem`, `releaseUse`, `attackEntity`, `setAttackHeld`, `setUseHeld`; `predictPath` |
| Events | `BlockChangeEvent`, `ChunkLoadEvent`, `ChunkUnloadEvent`, `PlayerTickEvent`, `BlockBreakProgressEvent`, `ContainerContentEvent`, `EntitySpawnEvent`, `EntityRemoveEvent`, `InventoryChangeEvent`; `MovementEvent` gains `startGlide`, `stopGlide`, `startFlying`, `stopFlying` and `swimDown` |
| Drawing | `WorldPainter::lines`, `wireBox`, `filledBox` and `text3d` without a shader of your own |
| `hud()` | `project` a world position onto the Canvas, `notify` for stacked notifications |
| `scheduler()` | `async(task, onDone)` runs work on a worker thread and finishes on the main thread |
| `ui()` | `addSettings` builds a settings page bound to the mod's config |
| `network()` | typed packet views from `mod/Packets.h`: `addTypedFilter`, `sendTyped`, and `ping` |
| Commands | `CommandSpec::complete` suggests arguments on Tab |
| Mods | `Mod::dependencies()` orders and ties mods together; `provide` and `service` share objects between them |

### Compatibility

Mods built for API 1 to 3 still load unchanged: every new member is appended after the existing ones, so the
layout older mods were built against stays the same. A mod that needs another one declares it through
`Mod::dependencies()`; it then starts after it and unloads with it.
