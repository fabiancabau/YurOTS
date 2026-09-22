Original prompt: Integrate this YurOTS server with https://github.com/Inconcessus/html5-tibia-client. User authorized forking and doing the integration.

- Created GitHub fork fabiancabau/html5-tibia-client; client checkout on codex/yurots-76.
- Server integration branch codex/browser-client. Original server is 0.9.4f, protocol 760.
- Implement a dedicated browser 7.6 mode using upstream sprite primitives, native protocol decoding and fixed-target WebSocket relay. Preserve original Forby client entrypoint.
- In progress: Linux/Docker server build, assets, client implementation, live browser verification.

## Implementation and validation

- Client fork implements native protocol 760, fixed-target binary WebSocket/TCP gateway, browser character selection, map decoding/rendering, movement, outfits, inventory/containers, item interactions, chat/channels, combat/effects, VIP, text windows, trade and party packet support.
- Reused upstream DAT/SPR parsing, sprite atlas and outfit compositing; corrected the real 7.6 layouts and atlas slot clearing.
- Linux ARM64 Docker server builds successfully. Fixed LP64 OTB/OTBM structures, Lua lightuserdata pointers, Linux configuration path, old Windows runtime calls, socket handoff and fragmented TCP reads. NPC lowercase aliases are prepared in runtime data.
- Assets downloaded only for local testing and ignored by Git. No client executable was run.
- Nine packet/framing regression tests passed.
- Live browser test passed: login, 2016 map tiles, server-confirmed walking, backpack (13 items), chat, outfit dialog and logout; no browser errors.
- Advanced live test passed: fragmented TCP login, bad password, inventory move roundtrip, healing spell, 4 channel choices, z=6 floor transition, combat/health/removal; no browser errors.
- The required develop-web-game Playwright loop was run with a local login setup added to its copied runner. Repeated movement screenshots and state show real server updates; no errors. Screenshots inspected.
- Found and fixed stale chat-channel selection when changing characters.
- Two-player live checks passed: party join, VIP online status, private messaging, both trade offers and trade cancellation. Fixed a queued dialog-close race that canceled the second offer. Trade screenshot inspected.
- Final Compose build/start passed. Both containers run with ports restricted to localhost; basic live browser smoke passed against the fresh Compose data volume.
- Client commit 3cf4b40 pushed to fabiancabau/html5-tibia-client, branch codex/yurots-76. Registered the fork as the client submodule.
- Local client: http://127.0.0.1:8080. Requested its browser panel in Codex.
- Scope limitations: test coverage does not claim full native-client feature parity or completed trade acceptance. The original Forby account-creation API is not used; YurOTS accounts remain authoritative.
- No further implementation work is required for the verified playable integration. Review/publish state is recorded in the task response.


## Huntera-inspired interface redesign

User request: make an interface like the supplied Huntera screenshot.

- Implementing full-viewport square-tile game view, dark beveled floating panels, compact character/nav topbar, paper-doll inventory, bottom vitals/actionbar, real session XP and editable spell hotkeys.
- Source screenshot is the visual reference; server remains authoritative.
- Added aspect/coordinate regression checks; 11 tests pass including existing native protocol coverage.
- Parallel work: HTML/CSS shell, isolated HUD behavior module, browser checks. Root integrates renderer and game actions.
- Initial live visual pass completed using the develop-web-game runner. Fixed half-pixel camera seams by using even buffer dimensions and pixel-aligned draw positions.
- Window controls, inventory, spell editor and keyboard focus checks are running against real YurOTS. Existing sample character inherited a combat lock, so a separate local HUD Verification character was created at the temple for repeatable reload/logout tests; source data was not modified.
- Session UI displays measured time/XP and actual level/progress. No invented player count, currency, cooldown or kill totals.

- HUD verification passed against the isolated test character: native login, logout/relogin, backpack, drag/visibility persistence, hotkey editing/persistence, Enter focus behavior, suppressed hotkeys while typing/dialogs, real F1 healing, keyboard and pointer walking. No browser runtime errors.
- Responsive screenshots inspected at 1440x900, 1920x1080, 1000x760, and 390x844. Fixed compact Battle/Friends overlap, mobile overflow, panel scaling, and restoring desktop positions onto smaller windows. Narrow screens now open one panel at a time and restore desktop state on return.
- Final source validation: 11 protocol/framing/viewport tests pass; checked diffs are whitespace-clean.
- Client HUD commit fa6e2d5 pushed. Updated localhost:8080 through Compose and ran the established browser action/screenshot loop against that exact deployment: real login, backpack, rendered game and clean logout passed. Final screenshot inspected; no browser error artifact was produced.
- Refreshed the existing in-app preview to the new interface. Development preview8081 can be stopped; Compose8080 remains the delivered app.


## Rooftop movement and stair rendering repair

User reported sprite clipping and a brief apparent underground step on the roof beside the stairs.

- Reproduced on the real map at 141,73,6 with an isolated Render Verification character. User's existing Yurez The Next session was left alone.
- Before: leftward animation stayed at z=6 but later source-tile ground covered the actor; visible outfit pixels dropped to 164 vs 376 at rest. Floor changes also reused a cached walk and left the camera at 141,74 while authoritative positions were 141,75,7 and 141,73,6.
- Fix: compose each floor's ground/borders before its scenery/creatures; depth-sort scenery and walking creatures by visual foot position; clear old walks on full-map updates/appearance; ignore stale/wrong-floor interpolation for camera/actors.
- After: same real left step keeps 356–376 outfit pixels visible throughout; camera matches server positions immediately on both stair directions. Screenshots inspected in client/artifacts/rendering.
- Added raster-order and interpolation regressions covering all eight directions, foreground walls, upper-floor occlusion and map-replacement reset.
- Seventeen focused tests pass, including in a clean exported client snapshot without the separate in-progress movement-queue edits.
- Rendering commit 4064cb7 pushed. Updated only served render files and floor-reset handling in the running localhost:8080 client, preserving active game connections and concurrent movement work. Exact live rooftop/stair reproduction passed after the update; user can reload when ready.

## Responsive movement queue and wall recovery

User request: make movement smooth when directions are queued or the player hits a wall.

- Found independent client/server causes: rejected server moves spent a cooldown; client callbacks could clear a newer pending move after one second; keyboard selection preferred the oldest held key and dropped quick taps; nominal timing ignored terrain.
- Added a controller with one authoritative request in flight, latest-intent buffering, most-recent held-key priority, deferred click path planning, collision preflight, bounded dynamic-blocker retries, and explicit cancellation ordering.
- Server cooldown now records successful self-movement only; diagonals retain double cost after movement, and blocked native auto-walk stops its remaining route.
- Testing in disposable worlds on ports 7172/7173, with gateways 8082/8083. The original server measured a 282ms delay to turn away after a rejected wall step. Added deterministic controller/protocol regression tests; live checks are in progress.
- Final live regression passed: raw wall-to-turn recovery 4.6ms versus the original 282ms; normal cardinal cooldown ~288ms and diagonal cooldown ~580ms remain enforced. Visible-wall turns responded in ~15ms without sending blocked walk packets.
- Verified quick taps, overlapping held keys, release/no extra steps, keyboard interruption of click paths, Escape, numpad diagonals, native blocked/empty paths, and 1250ms delayed network replies with only one request in flight. No browser errors.
- 33 Node tests pass (16 new movement regressions). Server image builds successfully. Ran the skill Playwright runner with local login setup; real single-step stairs changed 141,73,6 -> 141,75,7 and back to 141,73,6. Screenshots inspected; pending/buffered movement empty after each step.
- Saved the active local world with a confirmed `/save`, rebuilt both Compose images, and updated localhost:8080. The running server SHA-256 matches the isolated tested binary. A fresh deployed-browser login, keyboard move, empty pending/buffered state and logout passed; screenshot inspected, no errors. Changes remain local and uncommitted.
- Stopped the disposable test worlds and gateways. No remaining movement task TODOs; reproduce with `client/tests/movement-live.mjs` and the documented isolated temple fixture.


## Party instanced auto-hunts

User request: list available hunts, create private party instances (solo initially), and automatically hunt their spawns using the supplied Huntera catalog/detail references.

- Implemented server HuntManager in hunts.h/cpp: 247 real spawn-area definitions, reserved-coordinate map copies, party membership/access guards, server movement/targeting/combat/healing, capped pulls, native respawn timers, actual corpse loot transfer, pause/manual override/leave/disconnect cleanup, safe persisted return position.
- Added authenticated 0xF0 command/state extension and hunt UI with search/favorites, creature/loot details, pull size, autoloot, and active hunt controls. Preserved pre-existing movement-controller edits in client checkout.
- Disposable server yurots-hunts-test on7174 and preview gateway8084. Original localhost8080/7171 remains untouched during implementation.
- First live rat hunt passed: all247catalog entries, private coordinates, autonomous movement,2kills/100XP and real loot; pause/resume/return worked with no browser errors. Fixed collapsed catalog CSS tracks and monster icon framing after screenshot inspection.
- In progress: all-area validation, multi-player isolation/party joining, cleanup and save/reconnect checks, final docs/deployment.
- Full catalog validation passed: 247 areas, 0 failures, 1,136,106 temporary tiles cloned/cleaned. Improved connected-terrain selection for five isolated/blocked source spawn areas.
- Live isolation passed: distinct solo instances and monster IDs; authenticated party members share one instance; outsider GM teleport blocked; autosave stores public return coordinates; last-member cleanup returns allocated rooms/tiles/monsters to zero; reconnect restores the original position.
- A full original 60-second respawn cycle passed (4 kills, 200 XP), with pause/resume/leave continuing to work. Earlier runs verified actual loot transfer; a full backpack correctly leaves later drops in corpses.
- Death and explicit logout passed in the disposable world: hunt ends, private resources clean up, and the character returns to the temple/public world on login.
- Standard develop-web-game action/screenshot loop passed with real auto-kills and native server state. Catalog, detail and hunting screenshots inspected. Thirty-six Node regressions pass with the existing movement tests included.
- Preserved the completed movement controller as separate client commit 37bae91 before the hunt changes so the new client remains reproducible.
- Saved the live world through an authenticated `/save`, then updated Compose server/client. The deployed server binary SHA-256 matches the isolated tested binary (cd11e2aa1c7ca69854298c7c50a1e91b536d9f0376ef40b853ec70fa7a8717df).
- Deployed localhost:8080 smoke passed with the separate HUD Verification character: all 247 hunts, private Rat Cellars, 2 kills / 100 XP / 4 items, pause/resume and exact return position. No browser errors.
- Client hunt commit ee591c0 builds on the separately preserved movement commit37bae91. HUNTS.md documents semantics, limits and repeatable verification.
- Final combat boundary review added checks at the central spell/damage paths, including nonoffensive spells, indirect conditions and every area-effect tile. The server catalog validator exercises blocked cross-boundary casting and allowed same-instance casting; all 247 areas still pass.
- Re-ran the standard browser action/screenshot loop against that build: 3 kills / 150 XP with normal respawn waiting; inspected the full interface and no runtime error artifacts. Thirty-six client regressions still pass.
- Saved the local world again and deployed the exact verified image. Running and isolated server binaries match SHA-256 2047b34c393b604be35f45e4e056477430f79fa9a8813834e1f3dbe1d8350791. Final localhost:8080 smoke passed: 2 kills / 100 XP / 8 collected items, pause/resume, original return tile and no browser errors.
- Updated the existing client/server draft pull requests to describe the final auto-hunt integration. No remaining auto-hunt implementation TODOs.
