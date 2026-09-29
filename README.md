<p align="center">
	<picture>
		<source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/Falcon-MC/Falcon/main/.github/logo-white.png">
		<img src="https://raw.githubusercontent.com/Falcon-MC/Falcon/main/.github/logo.png" alt="Falcon" width="200">
	</picture>
	<br>
	<b>Kestrel</b>
	<br>
	Minecraft: Bedrock Edition client written in C++20
</p>

<p align="center">
	<img src="https://img.shields.io/badge/minecraft-v1.26.51%20(Bedrock)-56383E" alt="Minecraft">
	<img src="https://img.shields.io/badge/protocol-2193-blue" alt="Protocol">
	<img src="https://img.shields.io/badge/language-C%2B%2B20-00599C" alt="C++20">
	<img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey" alt="Platform">
</p>

## What is this?

Kestrel is a native Minecraft: Bedrock Edition client. It connects to servers and Realms with a Microsoft
account and renders the world with the textures of the installed game.

- **Rendering** - Direct3D 12 on Windows, Metal on macOS, Vulkan on Linux
- **World** - chunk streaming, multithreaded greedy meshing, block models, animated textures and
  translucent blocks
- **Atmosphere** - day and night cycle, sun, moon, fog and clouds
- **Account** - Microsoft sign-in with a device code, Xbox profile and Realms list
- **Chat** - the game's chat screen and fading HUD log, commands, and server messages in the chosen language
- **HUD messages** - titles, popups, tips, the action bar and server toasts, timed and placed like the game's HUD
- **Discord** - shows Minecraft activity while Kestrel is open, reconnecting if Discord starts later
- **JSON UI** - the HUD and server forms drawn from the game's hud_screen.json and server_form.json, restyled
  by whatever UI the server's packs ship
- **Server packs** - the packs' UI files, textures and glyph sheets

## Building

Requires CMake 3.24+, a C++20 compiler and zlib. Linux also needs the Vulkan SDK. The first configure
needs network access to fetch the dependencies.

```
cmake -B build -G Ninja
cmake --build build
```

When `Falcon-NBT`, `Falcon-Protocol`, `Falcon-Network` and `Falcon-BedrockData` sit next to this directory they are used
directly, otherwise they are fetched from GitHub. Set `KESTREL_FALCON_ROOT` to point at another directory.

The block textures are read from the installed game. Set `KESTREL_VANILLA_PACK` to use another vanilla
resource pack.

## Command line and agents

```
Kestrel --connect play.example.net:19132   join a server straight away, skipping the start screen
Kestrel --hidden --agent                   no visible window, frames drawn offscreen
Kestrel --headless --connect <address>     no window and no rendering, chat printed to stdout
```

`--agent` (or `--agent-port <port>`) opens a JSON control port on 127.0.0.1 for automation. The port and its
token are written to `agent.json` in the data directory; set `KESTREL_AGENT_TOKEN` to choose the token. The
[kestrel-mcp](https://github.com/Falcon-MC/kestrel-mcp) server uses it to let AI agents drive Kestrel: screenshots,
menus, input, forms, inventory and packet logs. Screenshots need the Vulkan renderer for now.

## Related repositories

- [Protocol](https://github.com/Falcon-MC/Protocol) - packets and network types
- [Network](https://github.com/Falcon-MC/Network) - RakNet and NetherNet transport
- [NBT](https://github.com/Falcon-MC/NBT) - NBT tags and binary streams
- [BedrockData](https://github.com/Falcon-MC/BedrockData) - game data files, versioned by protocol

## Licensing information

Kestrel is an independent, unofficial client. It is not approved by or associated with Mojang or Microsoft.
All brands and trademarks belong to their respective owners.
