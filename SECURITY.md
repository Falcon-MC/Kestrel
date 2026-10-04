# Security Policy

Kestrel connects to servers run by strangers and signs in with a Microsoft account. A vulnerability can mean a
server that takes over the player's machine, a stolen account, or files written where they should not be.
Reports are taken seriously, and we are grateful to everyone who takes the time to send one.

---

## Supported Versions

| Version                          | Supported                          |
|----------------------------------|------------------------------------|
| Latest release and `main`        | ✅ Fixes land here                  |
| Nightly builds                   | ✅ Report against the commit        |
| Older releases                   | ❌ Not supported, update first      |
| Forks and modified builds        | ❌ Report to the fork's maintainer  |

**Before reporting, reproduce the issue on the latest release or on `main`.** Fixes are not backported.

---

## Reporting a Vulnerability

**Do not open a public issue, pull request or discussion for a security vulnerability.** Public disclosure
before a fix exists puts every Kestrel player at risk.

Report it privately through **GitHub Private Vulnerability Reporting**:

👉 **[Report a vulnerability](https://github.com/Falcon-MC/Kestrel/security/advisories/new)**

This is also reachable from the repository's **Security** tab → **Report a vulnerability**. It creates a
private advisory visible only to you and the maintainers until it is published.

### What to include

- The Kestrel version or commit
- Operating system and renderer
- **Impact**: what an attacker gains, such as code execution, a crash, a leaked token or a file write
- **Reproduction steps**, ideally a minimal proof of concept such as a packet capture, a test server plugin,
  a resource pack or a script
- **Preconditions**: does the player have to join a specific server, accept a pack, open a form or run a mod?
- A suggested fix, if you have one

---

## Scope

### In scope

- **Remote code execution** or **memory corruption** triggered by anything a server sends: packets, chunks,
  NBT, skins, entity data, forms, JSON UI or chat
- **Malicious resource packs**: path traversal, files written outside the pack cache, decompression bombs,
  or crashes from crafted textures, models, UI files or particle definitions
- **Account compromise**: leaking the Microsoft, Xbox Live or PlayFab tokens to a server or a third party,
  or sending them anywhere other than Microsoft's own endpoints
- **The agent port**: anything that lets another local user or a web page drive Kestrel without the token
  from `agent.json`
- **Denial of service** that keeps Kestrel from starting again, for example a server that corrupts the saved
  settings or the server list

### Out of scope

- **Mods.** Mods are native code and run with the same rights as Kestrel. Only install mods you trust.
- **Anything requiring access to the player's machine or data directory** beforehand.
- **A server kicking, banning or lying to the player.** Servers are in control of their own gameplay.
- **Crashes from a server that are not exploitable.** File them as normal bugs.
- **Vulnerabilities in the official Bedrock client or in Microsoft services.** Report those to Mojang or
  Microsoft.
- **Dependency CVEs with no demonstrated impact on Kestrel.** Open a normal issue instead.

Not sure whether something is in scope? Report it privately anyway.

---

## What Happens Next

1. **Acknowledgement.** We aim to confirm receipt within a few days.
2. **Triage.** We reproduce the issue and share our assessment of its severity.
3. **Fix.** We work on a patch and keep you updated.
4. **Coordinated disclosure.** We agree on a disclosure date with you.
5. **Publication.** We publish a GitHub Security Advisory and credit you by name or handle, or keep you
   anonymous if you prefer.

---

## Disclosure Expectations

- Give us a chance to fix the issue before making it public.
- Only test with accounts, machines and servers you own. Do not attack other players.
- Stop at a proof of concept.

There is no paid bug bounty. What we can offer is credit in the advisory and in the release notes, and our
genuine thanks.

---

## For Players

- Download Kestrel only from the [releases page](https://github.com/Falcon-MC/Kestrel/releases) and verify the
  `.sha256` checksum.
- Never share `microsoft-token.json` or `agent.json` from the data directory. They are as good as your password.
- Only install mods from sources you trust. A mod can do anything Kestrel can.
- Only start Kestrel with `--agent` on machines you control.

Thank you for helping keep Kestrel and its players safe.
