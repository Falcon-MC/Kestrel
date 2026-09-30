# Vanilla client completion backlog

Audit date: 2026-09-30. Code baseline: `d616b09`.

This is a source-based backlog, not a claim that every vanilla feature has been exhaustively audited. No build, automated test, or live server session was run for this audit. A implemented system is not automatically verified as vanilla-equivalent.

## How to read this document

- **Confirmed placeholder**: a visible action or page explicitly contains a TODO.
- **Missing client path**: the inspected client dispatcher, model, or UI does not implement the named path. Protocol definitions in a dependency alone do not constitute client support.
- **Partial support**: implementation exists, with a concrete limitation identified below.
- **Validation required**: existing functionality must be checked; this label does not mean it is missing or broken.
- Checkboxes are actionable completion steps. Several steps may belong to one missing feature; they are not separate claims of missing vanilla features.
- Service-dependent features require verified API contracts and account permissions. Do not invent endpoints or simulate successful actions.

## Existing functionality: do not rebuild from scratch

The inspected tree already contains:

- Inventory transactions, response handling, prediction/resynchronization, crafting recipes, recipe UI, creative content, armor/offhand slots, and quick-move logic.
- Chest/container and furnace UI, including progress indicators and custom container names.
- Player motion, sneaking input, swimming, collision handling, server corrections, knockback, abilities, and respawn handling.
- Chat, commands, scoreboards, forms, boss events, titles, and HUD visibility handling.
- Entity geometry, skins, animation/Molang infrastructure, held-item rendering, and armor support.
- Sky/weather atmosphere, block/sky lighting, sound services, and a particle library/system/renderer.
- Resource-pack loading, CDN/network download infrastructure, caching, and diagnostics.
- Video controls for render distance, FOV, framerate, GUI scale, paper doll visibility, and safe area; keyboard rebinding, audio controls, and language selection.
- Realms listing/sign-in UI and dependency-side Realms operations.

These are implementation-presence observations, not complete parity certifications.

## P0 — Server compatibility and reliable entry into the world

**Validation required / previously observed failure.** A September 28 Hive log reported `persistent palette length exceeds 4096` and zero loaded columns. That historical log is not proof of the current executable's behavior. Zeqa's earlier timeout must be diagnosed separately.

Sources: [palette decoder](src/world/chunks/PalettedStorage.cpp), [sub-chunk decoder](src/world/chunks/SubChunk.cpp), [world stream](src/world/chunks/WorldStream.cpp), [session](src/client/session/Session.cpp). Transport/download code lives in the sibling Falcon-Network repository.

- [ ] Reproduce Hive on the current revision and record the exact failing stage.
- [ ] Reproduce Zeqa separately; do not assume it shares Hive's cause.
- [ ] Compare identical protocol-version connections against the working Cinnabar path.
- [ ] Capture bounded, non-sensitive chunk decoding diagnostics: version, storage header, offset, length, and first failing operation.
- [ ] Establish the actual wire format before changing palette interpretation again.
- [ ] Check palette counts, packed words, runtime IDs, and NBT entries against the received bytes.
- [ ] Preserve the original decoding error when trying an alternate encoding, rather than reporting only the fallback failure.
- [ ] Verify dimension ranges and explicit sub-chunk Y coordinates with the server data.
- [ ] Verify biome parsing and the boundary between sub-chunks, biomes, and block entities.
- [ ] Confirm successful entry produces loaded columns and rendered terrain, not only actors and skins.
- [ ] Confirm a long resource-pack download keeps the transport alive.
- [ ] Confirm disconnection/cancellation stops downloads and reports the actual reason.
- [ ] Confirm transfer/reconnect clears stale world and download state.
- [ ] Retain a reproducible regression case for each confirmed protocol defect, subject to authorization to run tests.

## P1 — Social and Realms (implemented, not yet built or run)

Implemented after `d616b09`, uncommitted. **No build, test or live session has been run**; every item below still needs runtime validation.

Endpoints come from established open-source clients (df-mc/go-xsapi, MCXboxBroadcast, PrismarineJS/prismarine-realms), not official documentation.

Sources: [social service](src/client/services/Social.cpp), [social model](include/client/SocialModel.h), [social/Realms screens](src/menu/SocialScreens.cpp), Falcon-Network `RealmsService`, `XboxSocialService`, `ServiceError`.

### Realms invitations

- [x] Invitation dialog from the Realms tab and the title-screen inbox button (`GET /invites/pending`).
- [x] Accept / decline per invitation (`PUT /invites/accept|reject/{id}`), with per-invitation pending state and no double submission.
- [x] Loading, empty, error, retry, and "no longer valid" states; refresh after revoked/processed invitations.
- [x] Realms list reloaded after acceptance; responses discarded on account change.
- [ ] Validate against a real pending invitation.

### Join a Realm by code or link

- [x] Input dialog with paste, submit, cancel; Enter/Escape.
- [x] Single parser `RealmsService::parseInvite` / `inviteCode`: host must be `realms.gg`, code `[A-Za-z0-9_-]`, bare code accepted in the dialog only.
- [x] Lookup shows the Realm, owner, already-member, closed and expired states, then accepts and connects through `realm_id/`.
- [x] Input kept after a recoverable error; stale responses ignored; codes never logged.
- [ ] Validate with a real link, an invalid link, and an already-member Realm.

### Friends and presence

- [x] Friends list (peoplehub `friends`) keyed by XUID, with gamertag, avatar (async, fallback, 48 max), presence, and activity.
- [x] Joinable / online / offline sections, stable sort, local filter, scrolling.
- [x] Joinability from Xbox multiplayer session handles, not presence; version mismatch and unsupported connection explained.
- [x] Join goes through `session_handle/<id>` and `MultiplayerSessionDirectory::join` in `Session`.
- [x] Refresh at most every 60 s while the drawer is open, plus manual refresh; data cleared on account change.
- [x] Remove friend with confirmation.
- [ ] Validate joining a friend's world (RakNet and NetherNet).
- [ ] Inviting a friend to the current game: **not possible**, Kestrel publishes no multiplayer session of its own.

### Friend requests and search

- [x] Received requests (accept/decline) and sent requests (cancel).
- [x] Player search by gamertag (Enter in the drawer); empty result separate from failure.
- [x] Send request only when `canBeFriended`; already-friends, already-requested, self states handled.
- [x] Rate limits honour `Retry-After`; mutations are never retried after an uncertain failure; lists reconciled after each mutation.
- [ ] Validate with a consenting test account only.

## P2 — Parties and inbox

- [x] Inbox button opens the Realms invitations (the only inbox content Kestrel can retrieve).
- [x] Party tab states that parties are unavailable; the fake "Create party" button is gone.
- [ ] Establish a supported party service before implementing parties.

## P2 — Settings pages

- [x] Pages without a backing capability (Accessibility, Controller, Touch, Party, General, Subscriptions, Global Resources, Storage, Creator) explain why instead of showing TODO; no inert controls.
- [ ] Accessibility: define the supported controls and connect them to actual UI/game behavior.
- [ ] Controller: needs controller input support first (none in `src/platform`).
- [ ] Touch: needs touch input support first.
- [ ] General: replace the empty page with version-appropriate, effective settings.
- [ ] Subscriptions: provide an authenticated supported service/link flow; do not fabricate entitlement management.
- [ ] Global Resources: provide a real pack selection/activation path, reusing pack infrastructure.
- [ ] Storage: provide actual storage inspection and clearly scoped management operations.
- [ ] Creator: define and implement the settings supported by this client.
- [ ] Menu keyboard focus between buttons and controller navigation do not exist yet.

### Brightness

- [x] Slider 0–100 %, default 0 (identical to the previous rendering), saved as `brightness=` with clamping of invalid values, exposed to the agent settings.
- [x] Applied immediately by lifting dark areas toward full light through the night-vision constant, up to 50 % at 100 % (all three backends, no shader change).
- [ ] This is Kestrel's own curve; compare with vanilla in daylight, darkness, and underwater before claiming parity.

### Completion contract for every newly implemented setting

These are acceptance tasks for the missing pages, not claims that existing settings fail them.

- [ ] Trace each control from UI to configuration to the subsystem that applies it.
- [ ] Persist values and handle invalid or legacy configuration values.
- [ ] Use existing localization keys or add appropriate keys and fallbacks.
- [ ] Explain restart/reconnection requirements where applicable.
- [ ] Support keyboard focus, cancellation, and the input modes available in the client.
- [ ] Respect GUI scale and long translated labels.
- [ ] Provide reset/default behavior appropriate to the page.
- [ ] Avoid active controls that only change an unused menu variable.

## P2 — Other explicit menu placeholders

Sources: [Marketplace routing](src/menu/Menu.cpp), [placeholder screen](src/menu/MenuScreens.cpp), [profile](src/menu/ProfileScreen.cpp).

- [x] Marketplace screen and the profile's featured screenshot action now state they are unavailable (translated, no TODO text).
- [ ] Implement Marketplace only after defining accessible catalog and entitlement capabilities; keep browsing separate from purchases.
- [ ] Implement the featured screenshot action once its service is identified.
- [ ] Known limitation: closing the client or signing out can wait up to ~15 s while a friend-session query is in flight (MPSD query is not cancellable).

## P1 — Specialized inventory and workstation paths

**Missing client paths in the inspected implementation.** `containerSize` explicitly handles common storage and furnace types, otherwise returning zero. `InventoryScreen` has crafting, furnace, and generic storage layouts. `InventoryModel::responseSlot` handles common storage/crafting/furnace slots but returns `-1` for other slot categories.

Sources: [session inventory](src/client/session/SessionInventory.cpp), [inventory model](src/client/inventory/Inventory.cpp), [inventory UI](src/menu/InventoryScreen.cpp).

- [ ] Add enchanting-table offers, costs, selection, and response reconciliation.
- [ ] Add anvil inputs, renaming, cost display, and server-authoritative output.
- [ ] Add brewing-stand slot rules and brewing/fuel progress.
- [ ] Add villager trade offers, selection, availability, and transactions.
- [ ] Add beacon configuration and payment handling.
- [ ] Add stonecutter recipe selection and output transactions.
- [ ] Add loom inputs, pattern selection, and output transactions.
- [ ] Add cartography-table inputs and map-result transactions.
- [ ] Add grindstone input/output handling.
- [ ] Add smithing-table slot and recipe handling for the targeted protocol version.
- [ ] Map each supported specialized container's response slot categories explicitly.
- [ ] Add the corresponding dedicated layouts instead of silently treating them as chests.
- [ ] Handle unsupported container types visibly until their paths are implemented.
- [ ] Preserve existing crafting, creative inventory, quick move, and storage behavior during these additions.
- [ ] Validate rejection, close-during-request, resynchronization, and delayed-response behavior for each new container.

## P1 — Additional protocol-driven gameplay paths

**Missing client paths found by source inspection.** Searches of `src` and `include` found no named client handling for the packet families below, and the inspected `Session::handleWorldPacket` whitelist does not dispatch them. Verify protocol names and any future alternative handling before implementing.

Source: [world-packet dispatcher](src/client/session/Session.cpp).

- [ ] Handle `SetActorLink` and represent passenger/vehicle relationships.
- [ ] Handle trade/equipment-window data such as `UpdateTrade` and `UpdateEquip` where required by supported containers.
- [ ] Handle map updates such as `MapItemData`, including a map texture update path.
- [ ] Connect updated map content to held and placed map rendering where supported.
- [ ] Implement the sign-editor opening and text-submission flow for the target version.
- [ ] Implement book reading/editing/submission paths, including `BookEdit` where applicable.
- [ ] Handle server camera instructions with appropriate reset/lifetime behavior.
- [ ] Implement NPC dialogue handling separately from the existing modal form system.
- [ ] Audit command-block and structure-block editing paths before exposing their interactions as supported.
- [ ] Record unsupported relevant packet types in bounded diagnostics so silent dispatcher drops can be distinguished from successful handling.

## P1 — Movement modes beyond existing walking and swimming

**Missing client paths in the inspected motion sources.** The current motion code includes swimming and ordinary movement. Searches found no elytra/gliding or vehicle-control implementation in `src/client/motion`, `PlayerMotion.h`, and the inspected session motion path.

Sources: [motion API](include/client/PlayerMotion.h), [motion implementation](src/client/motion/PlayerMotion.cpp), [session motion](src/client/session/SessionMotion.cpp).

- [ ] Add elytra activation/deactivation state and the appropriate protocol input flags.
- [ ] Implement gliding movement and server reconciliation using a version-matched reference.
- [ ] Integrate relevant equipment and ability restrictions into gliding transitions.
- [ ] Implement mounted-player positioning and camera behavior once actor links are handled.
- [ ] Add vehicle-specific control paths for supported boats and controllable mounts.
- [ ] Handle dismounting and server corrections while mounted.
- [ ] Audit low-height/crawling poses separately; no dedicated crawl path was found, but keyword absence alone is not a full collision audit.

## P2 — Pack and expression compatibility

### Block-state condition evaluator

**Partial support.** The block condition parser's function-call path recognizes block-state/property queries and returns an empty value for other calls. This is distinct from the broader animation scripting implementation; do not label all Molang absent.

Sources: [block condition parser](src/world/animation/Molang.cpp), [script engine](src/world/animation/MolangScript.cpp).

- [ ] Inventory expressions actually encountered in supported block packs.
- [ ] Identify expressions the block condition parser cannot evaluate correctly.
- [ ] Reuse the broader evaluator where appropriate or extend the restricted evaluator deliberately.
- [ ] Add diagnostics for unsupported expressions rather than silently implying full language support.

### Particles

**Partial support.** Unknown components are collected in `effect.unsupported`. `materialOf` distinguishes blend/add materials and defaults other names to alpha test. The particle system itself is present.

Source: [particle library](src/world/particles/ParticleLibrary.cpp).

- [ ] Surface unsupported component names in bounded developer diagnostics.
- [ ] Inventory unsupported components used by actual vanilla/server effects before choosing implementation work.
- [ ] Implement confirmed missing component behaviors with observable reference cases.
- [ ] Audit unknown material names rather than assuming the alpha-test fallback is faithful.
- [ ] Verify emitters, lifetimes, events, collision, lighting, and texture animation for supported components.
- [ ] Distinguish an unsupported effect from a corrupt or missing asset in diagnostics.

## P3 — Existing systems requiring parity verification

**Validation required, not an absence list.** Run these only when authorized. Keep observed mismatches separate from planned investigation.

- [ ] Inventory: crafting, creative picks, quick move, stack splitting, armor/offhand, and transaction rejection.
- [ ] Containers: double chests, ender chests, custom names, furnace timing, and server-switch resync.
- [ ] Motion: sneak edges, step heights, swimming transitions, liquids, knockback, teleports, and corrections.
- [ ] UI: GUI scale, safe area, scoreboard/chat placement, focus, and long localized text.
- [ ] Forms: all existing form types, image loading, cancellation, and server-initiated close.
- [ ] Rendering: held items, skins, armor, nametags, custom geometry, and texture paging.
- [ ] World: chunk boundaries, lighting propagation, weather, dimensions, and block entities.
- [ ] Audio: positional sounds, categories, stop events, and resource-pack overrides.
- [ ] Particles: vanilla effects and server-pack effects under representative scene loads.
- [ ] Network: cold/warm pack cache, redirects, chunk fallback, cancellation, transfer, and reconnect.
- [ ] Document version, server, input sequence, expected reference behavior, and actual result for every discrepancy.

## Scope decisions before claiming a complete Minecraft replacement

The inspected play menu routes to Realms and Servers. That proves the menu lacks a local-world route; it does not establish that a complete local server must belong in this multiplayer client's current scope.

Source: [play menu](src/menu/MenuScreens.cpp).

- [ ] Decide explicitly whether local worlds are part of the product target.
- [ ] If yes, separately plan creation/opening, persistence, simulation/server integration, pause behavior, and world settings after an architecture audit.
- [ ] Define the target Bedrock version and the deliberate visual deviations, if any.
- [ ] Define which platform-specific features are required on the supported desktop platforms.
- [ ] Keep inaccessible commercial/social service capabilities visible as limitations rather than claiming unsupported parity.

## Completion rules

- [ ] For each implemented item, update this document with the actual supporting code and remaining limitations.
- [ ] Mark implementation and runtime verification separately.
- [ ] Do not convert a placeholder into an inert control or fake success response.
- [ ] Preserve existing work and do not rebuild already implemented systems merely because they appear in validation sections.
- [ ] Do not claim exhaustive vanilla parity based on a source audit alone.
