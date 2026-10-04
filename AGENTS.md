# AGENTS.md

Instructions for AI coding agents working in the Kestrel repository.

If you are a human, read [CONTRIBUTING.md](CONTRIBUTING.md) instead. It is the authoritative document, and
where the two disagree, CONTRIBUTING.md wins.

---

## The project

Kestrel is a native Minecraft: Bedrock Edition client written from scratch in C++. It is not derived from the
official client: rendering, UI, networking and the protocol are implemented here and in this organisation's
other repositories.

- **Language:** C++20 (Objective-C++ for the macOS platform layer).
- **Build system:** CMake with Ninja, dependencies pulled with `FetchContent`.
- **Platforms:** Windows (Direct3D 12, MSYS2 UCRT64 gcc), Linux (Vulkan, SDL3) and macOS (Metal, clang), all
  built by CI.
- **License:** LGPL-3.0.
- **Branch:** `main`.
- **Versions:** `1.0.1+1.26.50`, Kestrel's own version, then the Bedrock version it speaks.

---

## Required reading

| File                               | Read it for                                                        |
|------------------------------------|--------------------------------------------------------------------|
| [CONTRIBUTING.md](CONTRIBUTING.md) | **Mandatory.** Contribution rules, AI disclosure, closing reasons. |
| [SECURITY.md](SECURITY.md)         | What counts as a vulnerability and how to report one privately.    |
| [README.md](README.md)             | Project overview, command line, the mod API.                       |

---

## Commands

| Task                          | Command                                                       |
|-------------------------------|---------------------------------------------------------------|
| Configure and build           | `./build.sh`                                                  |
| Configure and build (Windows) | `build.bat`                                                   |
| Incremental rebuild           | `cmake --build build`                                         |
| Regression tests              | configure with `-DKESTREL_BUILD_TESTS=ON`, then `ctest --test-dir build` |
| Example mods                  | configure with `-DKESTREL_BUILD_EXAMPLE_MODS=ON`              |

The first configure downloads dependencies and needs network access. `build.sh` and `build.bat` write their
output to `build.txt`. A change that does not compile is worse than no change.

`particle-effects` reads the vanilla resource pack of an installed game and fails without one. CI skips it.

---

## Layout

```
include/            Headers, mirroring src/
  mod/              The public mod API. Mods compile against it.
src/
  client/           The client loop, session, account and Realms services, player motion
  world/            Chunks, meshing, block and entity assets, particles, resource packs
  render/           rhi/ interface plus the d3d12/, metal/ and vulkan/ backends
  ui/, menu/        JSON UI, fonts, the HUD, menus, forms, the inventory
  audio/            Sound loading and playback (miniaudio)
  modding/          Mod loading and the host side of the mod API
  agent/            The JSON control port behind --agent
  platform/         win32/, macos/ and linux/ windows, input, clipboard and paths
shaders/            GLSL for the Vulkan backend, compiled to SPIR-V during the build
data/               Small tables and the app art, embedded into the binary
tests/              Regression tests
examples/mods/      Example mods
```

The network transport, protocol packets, NBT, block state upgrades and Bedrock data files live in separate
`Falcon-MC` repositories. Never edit their copies under `build/_deps`.

---

## Hard rules

1. **Never invent APIs.** Before calling a function, field, packet or JSON UI property, search the codebase
   and confirm it exists with that exact signature. If you cannot find it, say so.
2. **Never guess vanilla behaviour.** Packet formats, UI layout values, physics constants and texture paths
   must match the game. If you cannot verify a value, say which one.
3. **Never commit game assets.** Textures, sounds, UI JSON, fonts and models are read at runtime from the
   player's installation. Nothing taken from Minecraft goes into the repository, not even for tests.
4. **Keep every backend building.** A change to `render/rhi` must land in Direct3D 12, Metal and Vulkan, and a
   change to `platform/` must cover all three platforms or say why not.
5. **Keep mods compiling.** Changes to `include/mod` must stay source compatible unless the task says otherwise.
6. **One logical change per branch.** Report unrelated problems you notice instead of fixing them.
7. **No repository-wide reformatting** and no refactors outside the task.
8. **No new dependencies** without an issue first.
9. **No dead code** and no debug logging left in the diff.
10. **Do not weaken security checks** on anything a server sends, or on the agent port's token. If one is in
    your way, stop and explain.
11. **Do not touch** `.github/workflows/` or release automation unless that is the task.
12. **Do not commit, push or open a pull request** unless the human explicitly asks.

---

## Architecture

- **Platform code goes behind interfaces.** Windows, input, clipboard and file dialogs go through
  `include/platform`. New platform specific code belongs in `src/platform`. `main.cpp`, the agent server and
  Discord presence still carry a few `#ifdef` blocks of their own; don't add more elsewhere.
- **Rendering goes through `render/rhi`.** Game code talks to `Device`, never to a graphics API directly.
- **The server is untrusted.** Everything decoded from the network, from resource packs and from JSON UI files
  can be malformed or hostile. Validate sizes and indices, and never let bad data crash the client.
- **Threads.** The session's network work and the mesh scheduler run off the main thread. The UI reads the
  state the session publishes under `Session`'s mutex. Don't reach into session internals from the render
  thread without it.
- **The game version lives in several places.** A Bedrock update changes `GameVersion` and `ProtocolVersion`
  in `src/client/session/Session.cpp`, `GameVersion` in `src/client/services/Account.cpp` and
  `src/client/services/FeaturedServers.cpp`, the version label in `src/menu/Menu.cpp`,
  `FALCON_BEDROCK_DATA_PROTOCOL` in `CMakeLists.txt` and the README badges. The release workflow refuses a tag
  whose Bedrock part disagrees with the code.

---

## Best practices

- **Match the surrounding file.** Consistency with neighbouring code beats your preferred style.
- **Search before writing.** Look for an existing helper first: `ui/Utf8.h`, `util/Text.h` and `util/JsonText.h`
  already cover most text work.
- **Prefer early returns** over nested conditions.
- **Comments explain why, never what.** Only comment why the code does something unusual, logic that is hard
  to follow, surprising behaviour of the game or an API, and performance decisions. Don't comment code that
  reads clearly on its own.
- **Know the hot paths.** Meshing, chunk decoding, particle updates and every per frame draw call run
  constantly. Avoid allocations and full scans there.

---

## Testing

Build, then run the tests. Beyond that, you usually cannot see the client running. Never write "tested in
game" unless you actually drove a running client.

If the human has given you the `kestrel-mcp` server, you can: launch Kestrel hidden, join a server, take
screenshots and read the game state through the `--agent` control port. Say exactly what you checked that way.
Otherwise hand the human a concrete test plan: what to do in the client, on which platform, what to look for,
and what would mean failure.

---

## What you must tell your human

1. **Your exact model name and version**, and which parts of the work you did.
2. **What you verified and how**, and a test plan for the rest.
3. **Anything you were unsure about**: an API you could not fully verify, a vanilla value you could not
   confirm, a platform you could not build, a case you did not handle.

Do not write pull request descriptions, review replies or issues for the human. Give them the facts to write
from.

---

## Security

Found something exploitable, such as a crash or code execution reachable from a server or a resource pack, or
a way to leak the player's tokens? **Do not open a public issue or pull request, and do not push a proof of
concept to a public branch.** Tell the human directly and point them to [SECURITY.md](SECURITY.md).

---

## Working style

- Read before you write.
- Prefer the smallest change that fixes the problem.
- When the task is ambiguous, ask instead of guessing.
- If you cannot do part of the task, say which part and why.
