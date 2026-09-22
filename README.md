# YurOTS in the browser

YurOTS 0.9.4f / Tibia 7.6, with a browser client forked from [Inconcessus/html5-tibia-client](https://github.com/Inconcessus/html5-tibia-client). The server remains authoritative; the browser implements the native 760 protocol and a small WebSocket gateway relays framed TCP packets.

## Run

Requirements: Docker with Compose. For browser tests, Node.js 20+.

```sh
git submodule update --init --recursive
# Put your Tibia 7.6 Tibia.dat and Tibia.spr in client/data/76/,
# or select them together in the browser's Game assets panel.
docker compose up -d --build
```

Open **http://127.0.0.1:8080**. The supplied data pack includes account **111111**, password **tibia**, with Yurez, GM Yurez and Yurez The Next. This is a local development setup: both published ports bind to localhost. Game data is copied into a Docker volume on first startup and persists across restarts. The original `ots/data` files are not changed by play.

```sh
docker compose logs -f server
docker compose down     # preserves world data
```

After modifying server source, run `docker compose up -d --build`. Game configuration lives in `/var/lib/yurots/config.lua` inside the server container. Update that persisted copy when changing an existing world's settings.

For client development with live file reload, run only the server in Docker, then use Node for the gateway:

```sh
docker compose up -d server
cd client
npm ci
npm start
```

The client is a Git submodule pointing to [fabiancabau/html5-tibia-client](https://github.com/fabiancabau/html5-tibia-client), branch `codex/yurots-76`. Commit client changes in that repository before updating the submodule reference here.

## Auto-hunts

Use the crossed-swords **Organize hunt** button to browse 247 source areas and start a private party instance with server-controlled hunting. See [HUNTS.md](HUNTS.md) for controls, isolation, respawns, loot and verification.

## Controls

- Arrow keys or numpad: walk; Ctrl + arrows: turn; click a reachable tile: walk there.
- Right click: look, use, use with, attack, trade or party actions.
- Double click a container: open it. Drag items between equipment, containers and the map.
- Enter: chat. Type spell words in Say. Private messages use `@Name@message`.
- F1–F8: spell shortcuts; right click a shortcut to change its words.
- Drag window title bars to reposition them; use the top toolbar to show/hide panels. Layout is saved in the browser.
- Escape: cancel targeting and walking. F: full screen.

## Verify

With the server and gateway running and the 7.6 assets installed:

```sh
cd client
npm ci
npx playwright install chromium
npm test
npm run test:live
npm run test:advanced
npm run test:social
```

The browser tests use the bundled sample accounts and modify the isolated local world. They write screenshots and state snapshots to `client/artifacts/`. The advanced test exercises fragmented TCP login, rejected credentials, inventory transfer, spells, channels, a GM floor transition and combat.

## Implementation

- `ots/source/CMakeLists.txt`, `Dockerfile`: Linux build (verified on ARM64) with Lua 5.1, Boost Regex and libxml2.
- `ots/source`: targeted fixes for 64-bit file formats, Lua object pointers, Linux networking, portable runtime functions and partial TCP reads.
- `client/yurots/`: native 7.6 packet decoder, UI and renderer using the upstream DAT/SPR readers, sprite atlas and outfit compositor.
- `client/gateway/`: same-origin WebSocket endpoints, fixed server destination and TCP packet framing. Credentials travel in binary login payloads, never URLs or server logs.

The original custom-protocol Forby client remains at `/index.html`; `/` opens the YurOTS mode. The gateway connects to `YUROTS_HOST` / `YUROTS_PORT` (defaults `127.0.0.1:7171`). `PORT` and `BIND` configure HTTP; `PUBLIC_ORIGIN` explicitly permits a different browser origin when running behind an HTTPS reverse proxy. The relay deliberately ignores the IP advertised in the character list and connects only to its configured YurOTS server.

The integration targets this repository's 7.6 server. It does not implement later Tibia versions, RSA/XTEA or the original Forby account-creation API. Existing YurOTS accounts and its in-game account maker remain the account source.
