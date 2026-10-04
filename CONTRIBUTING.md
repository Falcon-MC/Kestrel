# Contributing to Kestrel

Thanks for taking the time to contribute. Bug fixes, vanilla parity work, rendering, performance, mod API
additions and documentation improvements are all welcome.

**Before you open a pull request, read [Pull Requests](#-pull-requests) and [AI Tool Usage](#-ai-tool-usage).**
Those two sections describe what gets pull requests closed.

**Found a security vulnerability?** Do **not** open a public issue. See [SECURITY.md](SECURITY.md).

Everyone taking part is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

---

## 🌱 New to Open Source?

- [Finding ways to contribute on GitHub](https://docs.github.com/en/get-started/exploring-projects-on-github/finding-ways-to-contribute-to-open-source-on-github)
- [Setting up Git](https://docs.github.com/en/get-started/getting-started-with-git/set-up-git)
- [Understanding GitHub flow](https://docs.github.com/en/get-started/using-github/github-flow)
- [Collaborating with pull requests](https://docs.github.com/en/github/collaborating-with-pull-requests)

Read the [README](README.md) first for an overview of the project.

---

## 📋 Issues

### Reporting a bug

[Search existing issues](https://github.com/Falcon-MC/Kestrel/issues) first. Yours may already be tracked or
fixed. If not, [open a new issue](https://github.com/Falcon-MC/Kestrel/issues/new/choose) and include:

- The Kestrel version or commit
- Your operating system, GPU and renderer (Direct3D 12, Metal or Vulkan)
- The server you were on, if the bug happens in game
- Exact steps to reproduce
- Expected vs. actual behaviour, compared with the official Bedrock client
- `debug.txt` from the data directory, including the full crash output if there is one

The data directory is `%APPDATA%\Kestrel` on Windows, `~/Library/Application Support/Kestrel` on macOS and
`~/.local/share/Kestrel` on Linux.

If the bug only happens with a mod loaded, say so explicitly and name the mod.

### Working on an issue

Issues are not pre-assigned. Pick one and open a pull request. For anything large or architectural, comment
on the issue first so the approach can be agreed on before you write the code.

---

## 🔧 Making Changes

1. **Fork** the repository and clone your fork.
2. Install the toolchain:

   | Platform | Requirements                                                                                      |
   |----------|---------------------------------------------------------------------------------------------------|
   | Windows  | [MSYS2](https://www.msys2.org) UCRT64 with `gcc`, `cmake`, `ninja`, `openssl`, `zlib`            |
   | Linux    | `g++` with C++20 support, `cmake` 3.24+, `ninja`, OpenSSL, zlib, the Vulkan SDK with `glslc`, and the X11 and Wayland development headers |
   | macOS    | Xcode command line tools, `cmake`, `ninja`, `openssl@3`                                           |

3. Branch off `main`.
4. Build:

   | Command       | Purpose                                 |
   |---------------|-----------------------------------------|
   | `build.bat`   | Configure and build on Windows          |
   | `./build.sh`  | Configure and build on Linux and macOS  |

   The first configure downloads the dependencies with CMake `FetchContent`, so it needs network access
   and takes a while. The executable is written to `build/`.

   Pass `-DKESTREL_BUILD_TESTS=ON` to build the regression tests and run them with
   `ctest --test-dir build`. `-DKESTREL_BUILD_EXAMPLE_MODS=ON` builds the mods in
   [examples/mods](examples/mods).

5. **Run Kestrel and test your change on a real server** before opening a pull request.

### Repository layout

| Path                  | Contents                                                              |
|-----------------------|-----------------------------------------------------------------------|
| `src/client`          | The client loop, session, account and Realms services, player motion  |
| `src/world`           | Chunks, block and entity assets, particles, resource packs            |
| `src/render`          | The Direct3D 12, Metal and Vulkan backends                            |
| `src/ui`, `src/menu`  | JSON UI, fonts, the HUD, menus, forms and the inventory               |
| `src/audio`           | Sound loading and playback                                            |
| `src/modding`         | Mod loading and the host side of the mod API                          |
| `src/agent`           | The JSON control port used by `--agent`                               |
| `include/mod`         | The public mod API that mods compile against                          |
| `shaders`             | GLSL sources for the Vulkan renderer                                  |

The network layer, the protocol, NBT and the Bedrock data files live in their own repositories under
[Falcon-MC](https://github.com/Falcon-MC). Changes to them go there.

---

## 🎨 Code Quality & Style

- Match the **existing style** of the file you are editing. When in doubt, copy the surrounding code.
- **One logical change per pull request.** Split unrelated fixes into separate pull requests.
- **No unrelated churn**: no drive-by reformatting, include reordering or refactors outside your scope.
- **Vanilla is the reference.** Rendering, UI and movement changes must match what the official Bedrock
  client does. Explain in the pull request how you compared them, screenshots side by side are ideal.
- **Keep all three renderers working.** A rendering change that only lands in one backend needs a reason in
  the pull request.
- **Reuse before you write.** Look for an existing helper or system before adding a new one.
- **Don't break mods.** Changes to `include/mod` must stay source compatible unless the pull request says why.
- **Never ship game assets.** Textures, sounds, UI files and fonts are read from the player's installation.
  Do not commit files taken from Minecraft.
- **No dead code** and no leftover debug logging.
- **New dependencies need justification.** Open an issue before adding one.
- **Commit messages** follow [Conventional Commits](https://www.conventionalcommits.org):
  `fix: keep the hotbar selection after respawn`, `feat: add spyglass zoom`, `refactor: split the session`.

---

## 📬 Pull Requests

- [Link the issue](https://docs.github.com/en/issues/tracking-your-work-with-issues/linking-a-pull-request-to-an-issue)
  with `Closes #123` or `Fixes #123`.
- Enable **"Allow maintainer edits"**.
- Open a **draft pull request** if the work is not finished.
- **CI must pass** on Linux, Windows and macOS.
- **Every pull request must be tested in a running client.** Describe exactly what you tested, on which
  platform and server. "Tested" alone is not a test report.
- Reply to review feedback and resolve conversations once addressed. Push follow-up commits rather than
  force-pushing while a review is in progress.

### Pull requests closed without review

- Code that does not compile, or that fails CI with no follow-up.
- Untested changes, or a testing section that is empty, generic or made up.
- Calls to functions, classes or packets that do not exist in this codebase.
- Repository-wide reformats, blanket "optimisations" or refactors nobody asked for.
- Files copied from Minecraft or from projects with an incompatible license.
- Undisclosed AI usage.
- Duplicate or spam pull requests.

You are welcome to fix the underlying problems and open a new one.

---

## 🤖 AI Tool Usage

AI tools are allowed. Hiding them, or submitting their output unread, is not.

- **Disclose it.** State which model you used and which part of the work it did: code, research, commit
  messages. Name the model and version, not just the vendor.
- **Write your own prose.** Pull request descriptions, issues and review replies must be in your own words.
  Translators and spell checkers are fine.
- **Own the output.** You must have read every line you submit and be able to explain it.
- **No AI-only bug reports.** Reproduce the bug in a real client before opening an issue.

---

## ⚖️ Licensing

- Contributions are licensed under the **[LGPL-3.0](LICENSE)**, like the rest of Kestrel.
- Do not copy code from projects with an incompatible license.
- If your contribution is derived from another open-source project, name the source and its license in the
  pull request.

---

## 🤝 Conduct

- Be kind, patient and constructive, especially with newcomers.
- Critique code, not people.
- Maintainers have the final say on what fits the project. A closed pull request is not a personal rejection.

Happy contributing! 🚀
