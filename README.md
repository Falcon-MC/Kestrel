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

## Building

Requires CMake 3.24+, a C++20 compiler and zlib. Linux also needs the Vulkan SDK. The first configure
needs network access to fetch the dependencies.

```
cmake -B build -G Ninja
cmake --build build
```

When `Falcon-NBT`, `Falcon-Protocol` and `Falcon-Network` sit next to this directory they are used
directly, otherwise they are fetched from GitHub. Set `KESTREL_FALCON_ROOT` to point at another directory.

The block textures are read from the installed game. Set `KESTREL_VANILLA_PACK` to use another vanilla
resource pack.

## Related repositories

- [Protocol](https://github.com/Falcon-MC/Protocol) - packets and network types
- [Network](https://github.com/Falcon-MC/Network) - RakNet and NetherNet transport
- [NBT](https://github.com/Falcon-MC/NBT) - NBT tags and binary streams
- [BedrockData](https://github.com/Falcon-MC/BedrockData) - game data files, versioned by protocol

## Licensing information

Kestrel is an independent, unofficial client. It is not approved by or associated with Mojang or Microsoft.
All brands and trademarks belong to their respective owners.
