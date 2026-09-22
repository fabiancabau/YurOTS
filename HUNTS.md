# Private auto-hunts

Open **Organize hunt** (the crossed-swords button) after logging in. Search the catalog or mark favorites, choose an area, choose a pull size, then start the private hunt.

The catalog contains all **247 spawn areas** in `ots/data/world/test-spawn.xml`, covering **94 creature types**. Repeated names refer to different source locations; each card shows its sector and floor. Creature stats and possible loot come from the server's loaded monster/item definitions. Loot percentages show the highest configured chance among that area's creatures, not an estimated payout.

## Playing

- Cautious, Daring and Aggressive cap the instance at 1, 3 and 5 active monsters respectively. Every source spawn participates as slots become free, using its original respawn timer.
- Auto-hunt chooses reachable monsters, walks toward them and attacks through YurOTS's normal combat system. It tries the character's normal `exura` spell below 65% health, respecting learned spells, mana and exhaustion.
- Auto-loot transfers actual generated corpse items using normal inventory/capacity checks. Items that do not fit remain in the corpse. There is no automatic selling or synthetic gold reward.
- **Pause** stops automation; monsters remain live. Manual movement/targeting also pauses automation. **Resume** continues it.
- **Leave hunt** returns to the tile where the character entered. Logout/disconnect ends that member's hunt too.
- Current online party members are captured when the first member creates the instance. Those members can select the same hunt to join it; they are not teleported without joining. Solo players each get their own instance.

The session controls show actual kills, XP gained, collected item count, party-member count, activity and respawn waiting time. XP, damage, deaths and items remain server authoritative.

## Isolation and lifecycle

Instances occupy non-overlapping reserved map cells starting at 4096,4096; up to64 instances can exist at once. The selected region is copied with its terrain/scenery. Original creatures, houses, external portals, quest IDs, container contents and pickupable map rewards are not copied. Monster spawn points are normalized onto reachable terrain within that copied region; isolated platforms or blocked lava spawns use a connected nearby floor from the same copy.

Travel into an instance requires membership; ordinary walking/teleports cannot cross its boundary. Targeted attacks, spells, runes and area effects also respect that boundary, including healing and game-master casts. Normal saves write each participant's public-world return coordinate, so a crash or restart cannot strand them in a discarded map copy. The last member leaving schedules monster and tile cleanup after one second. Hunt instances and hunt history are intentionally temporary; ordinary character XP/inventory persistence is unchanged.

## Verification

Use a disposable world for these tests; they fight monsters and alter the sample characters' XP and inventory.

```sh
# Offline creation/cleanup validation of every catalog entry:
docker run --rm yurots-local --validate-hunts

cd client
npm test
CLIENT_URL=http://127.0.0.1:8084 npm run test:hunts
TEST_RESPAWN=1 CLIENT_URL=http://127.0.0.1:8084 npm run test:hunts
CLIENT_URL=http://127.0.0.1:8084 HUNT_TEST_CONTAINER=yurots-hunts-test npm run test:hunts-isolation
```

The live tests expect the supplied sample accounts. `TEST_ACCOUNT`, `TEST_PASSWORD` and `TEST_CHARACTER` can override the single-player hunt test. The isolation test uses all three sample characters plus Docker access to verify the saved return coordinate. Runtime evidence/screenshots are written to `client/artifacts/hunts/` and `client/artifacts/hunt-isolation/`.

The server uses an authenticated native-protocol extension (`0xF0`, schema1). Old native clients receive no hunt messages unless they request this feature. Game-master-only diagnostics report allocated instance/tile/member/monster counts for lifecycle verification; they do not create or join a hunt.
