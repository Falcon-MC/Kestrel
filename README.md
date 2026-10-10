# Pull request screenshots

This branch stores Kestrel screenshots separately from implementation branches.
Do not merge it into application branches. Group captures by feature or fix and
link to an immutable commit in pull request descriptions.

## Spectator rendering

Captured from Kestrel on Windows / Direct3D 12, connected to the local Bedrock
server at `127.0.0.1:19134`.

- `spectator/normal.png`: creative mode, complete player and held bow.
- `spectator/third-person.png`: spectator mode, translucent head and invisible body.
- `spectator/first-person.png`: spectator mode, no hand or held item.

These are rendered screenshots, not extracted game assets.

## Mob animation weights

Windows / Direct3D 12, same local Bedrock server. An adult test pig was allowed
to walk, then stopped with Slowness 255. Its position was sampled repeatedly.

- `mob-animation/before-stopped-close.png`: stationary pig with fully bent walking legs before the correction.
- `mob-animation/before-stopped.png`: wider view of the stopped pig before the correction.
- `mob-animation/before-stopped-later.png`: same camera three seconds later; the pig remains at the same position with bent legs.
- `mob-animation/after-stopped.png`: legs return to neutral after walking and stopping with the correction. Two later samples had identical position and rotation.
- `mob-animation/after-idle-profile.png`: another corrected test pig, front view after walking and stopping.
- `mob-animation/after-idle-alternate.png`: alternate angle of the same corrected pig, with neutral legs while its head tracks the player.

The pig also resumed movement after Slowness was cleared and a carrot was held.
No game model or UI file was changed: the animation runner now applies the numeric
`scripts.animate` blend weight instead of converting it to a boolean. This matches
the [documented animation blend expressions](https://learn.microsoft.com/en-us/minecraft/creator/documents/animations/animationsoverview?view=minecraft-bedrock-stable).

The regression failed before the correction; the Windows build and all 55 tests
passed afterwards. These are Kestrel before/after captures, not an official-client
comparison. The test pig was removed and the player's inventory, position and FOV
were restored.

## In-game issue audit

Captures in `issues-2026-10-10/` support issues
[72](https://github.com/Falcon-MC/Kestrel/issues/72) through
[81](https://github.com/Falcon-MC/Kestrel/issues/81): crossbow charging, fishing
line, pumpkin mask, Blindness fog, sign editor, fire overlay, clock dial,
precipitation, boat model and Nausea distortion. Each issue records its live
Kestrel MCP reproduction and inspected source paths. Images are linked from
immutable commits. No official-client side-by-side comparison was performed.
