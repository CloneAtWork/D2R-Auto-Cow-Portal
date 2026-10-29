# D2R Auto Cow Portal

**A [D2RLoader](https://www.nexusmods.com/diablo2resurrected/mods/1119) plugin for Diablo II: Resurrected that opens the secret cow level portal in the Rogue Encampment automatically, for every difficulty you have unlocked. You no longer need to fetch Wirt's Leg or buy a Tome of Town Portal.**

[![build](https://github.com/CloneAtWork/D2R-Auto-Cow-Portal/actions/workflows/build.yml/badge.svg)](https://github.com/CloneAtWork/D2R-Auto-Cow-Portal/actions/workflows/build.yml)
![game build](https://img.shields.io/badge/D2R-3.3.0%20build%2093847-8b0000)
![license](https://img.shields.io/badge/license-MIT-blue)

---

## Features

- **Automatic portal.** The red Moo Moo Farm portal opens in the Rogue Encampment once per game. It is the same portal the Horadric Cube recipe (Wirt's Leg + Tome of Town Portal) creates.
- **Only where unlocked.** A difficulty counts as unlocked when its final quest is done: *Eve of Destruction* (Baal) in Lord of Destruction / Reign of the Warlock, or *Terror's End* (Diablo) in Classic. This is the same rule the game itself applies. Locked difficulties get no portal and no "I can't" sound.
- **Every way into town.** It works when the game starts in the Rogue Encampment, when you arrive by waypoint from another act, and when you walk or use a waypoint inside Act 1. The portal appears about 0.3–0.5 s after you arrive.
- **Fixed, tidy spot.** The portal is placed next to the central campfire. The town has four layout variants, so absolute coordinates would not work; the campfire is the anchor. The offset is configurable, and the game picks the nearest free, walkable spot.
- **Safe by design.** Before installing anything, the plugin verifies the game build and every game function it uses, byte by byte. After a game patch it does nothing and logs why.
- **D2RMM friendly.** The release includes a zip that D2RMM imports directly.

> The Cow King restriction from classic D2 does not exist in D2R 3.3.0. Killing him does not block the portal, with or without this plugin.

## Requirements

| | |
|---|---|
| Game | Diablo II: Resurrected **3.3.0 build 93847** (see [Limitations](#limitations)) |
| Loader | [D2RLoader](https://www.nexusmods.com/diablo2resurrected/mods/1119) with plugin ABI 4 (tested with 1.3.0-beta) |
| Mode | Offline single player, or as **host** of a TCP/IP game |
| Optional | [D2RMM](https://www.nexusmods.com/diablo2resurrected/mods/169) for one-click installation |

## Installation

Download the latest release from **[Releases](https://github.com/CloneAtWork/D2R-Auto-Cow-Portal/releases)**:

| File | Use it for |
|---|---|
| `cow-portal-in-town-<version>-d2rmm.zip` | Installation through D2RMM |
| `cow-portal-in-town-<version>.zip` | Everything: README, the D2RMM zip, and a `manual\` folder for manual installation |
| `SHA256SUMS.txt` | Checksums |

### With D2RMM
1. Close the game.
2. Copy `cow-portal-in-town-<version>-d2rmm.zip` **without unpacking it** into D2RMM's `d2rloader` folder, e.g. `D:\D2RMM Custom 1.9.9\d2rloader\`.
3. Start D2RMM (or use **Plugins > Refresh**), then install your mods as usual.

### Manually
1. Close the game.
2. Copy `manual\plugins\cow-portal-in-town.dll` to `<game folder>\d2rloader\plugins\`.
3. Optional: copy `manual\config\cow-portal-in-town.toml` to `<game folder>\d2rloader\config\`. If you skip this, the plugin creates the file on first start.

Use only **one** of the two methods, and keep only one copy of the DLL. Details, the load check and uninstall steps are in **[docs/INSTALL.md](docs/INSTALL.md)**.

## Configuration

`cow-portal-in-town.toml` (in `d2rloader\config\`, or in `Mods\D2RMM\d2rloader\config\` when installed through D2RMM):

```toml
[cow_portal]
enabled = true          # false = install nothing
normal = true           # per-difficulty switches
nightmare = true
hell = true

[placement]
anchor = "bonfire"      # "bonfire" = fixed spot near the campfire, "player" = next to you (like the cube)
offset_x = 6            # sub-tiles from the campfire: +x = down-right on screen
offset_y = 9            #                              +y = down-left on screen
fallback_to_player = true

[diagnostics]
enabled = false         # true = log every arrival and decision
```

All options and examples are in **[docs/CONFIGURATION.md](docs/CONFIGURATION.md)**.

## Limitations

- **One game build only.** The plugin calls the game's own functions at fixed addresses inside D2R 3.3.0 build 93847. On any other build it refuses to load and logs `Unsupported game build` or `Expected bytes mismatch`. A new release is needed after a game patch; [docs/UPDATING.md](docs/UPDATING.md) describes the process.
- **Host only.** In a TCP/IP game only the host's copy of the plugin matters. Players who join the host see the portal too; for them it opens at the latest ~10 s after they arrive.
- **Plugin history.** This is a gameplay (*Server*) plugin, so D2RLoader records it in the character's plugin history. Save files are not modified; the portal only exists in the running game, exactly like a cube-made one.
- **Offline only.** It is not for Battle.net; D2RLoader is offline/TCP-IP anyway.

## Troubleshooting

1. Check `d2rloader\logs\d2rloader.log` for `Cow Portal in Town … by Alex`.
2. Check the plugin log, `cow-portal-in-town.log`, next to it (or in `Mods\D2RMM\d2rloader\logs\`). It should contain `Hooks installed … Ready.`
3. Set `[diagnostics] enabled = true` and reproduce the problem. Every arrival and decision is then logged.

The meaning of each log line is listed in [docs/INSTALL.md#troubleshooting](docs/INSTALL.md#troubleshooting). If you open an issue, attach both logs.

## How it works

```
 arrival in Rogue Encampment ──► schedule ──► client finished loading? ──► campfire spawned? ──► open portal
   (hooks + 5-frame scan)          (once)      (LevelChanged event +0.5 s)   (or fallback)       (cube arguments)
```

| Step | Game / loader function (build 93847) |
|---|---|
| Notice arrivals | `QUESTS_ChangeLevel` 0x5155C0, `CLIENTS_SpawnPlayer` 0x485350, plus a scan every 5 frames in `GAME_UpdateProgress` 0x406550 (cross-act waypoints call neither hook) |
| Wait for the client | D2RLoader `LifecycleService` event *LevelChanged → Rogue Encampment*, then 12 frames |
| Check unlock | the game's own quest flags: A5Q6 or A4Q2 "reward granted", and the per-game flag A1Q4/CUSTOM7 "already opened" |
| Place and open | campfire object (class 39) + offset → `QUESTS_GetFreePosition` 0x517F90 → `CreatePortalObject` 0x432CE0 with the cube's arguments → set A1Q4/CUSTOM7 |

The full reverse-engineering notes, evidence, struct offsets and design decisions are in **[docs/TECHNICAL.md](docs/TECHNICAL.md)**. The step-by-step development log is in [docs/devlog/](docs/devlog/).

## Building from source

```bash
git clone --recurse-submodules https://github.com/CloneAtWork/D2R-Auto-Cow-Portal.git
cd D2R-Auto-Cow-Portal
./build.sh            # -> build/cow-portal-in-town.dll
./build.sh package    # -> dist/*.zip + SHA256SUMS.txt
```

You need MinGW-w64 GCC 13+ (Linux, or MSYS2 on Windows). Tests and the diagnostic probe are covered in **[docs/BUILDING.md](docs/BUILDING.md)**.

## Repository layout

```
src/            plugin source (C++20), resource script, default config
tests/          host + game simulator (61 checks) and runner
tools/probe/    cowportal-probe: read-only diagnostic plugin that dumps the running game image
tools/re/       d2img.py: static-analysis helpers for that dump (xrefs, disassembly)
tools/package.py  builds the release zips
external/PluginSDK  D2RLoader Plugin SDK (git submodule, pinned)
docs/           installation, configuration, building, technical notes, updating, devlog
```

## Credits

- [D2RLoader](https://www.nexusmods.com/diablo2resurrected/mods/1119) and its [Plugin SDK](https://github.com/D2RLoader/PluginSDK) (MIT)
- [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) by The Phrozen Keep community (MIT). Its reimplementation of D2 1.10 was the map for finding the matching code in D2R. No D2MOO code is copied.
- D2RMM for its plugin import

## Disclaimer

This is a fan-made, unofficial mod. It is not affiliated with or endorsed by Blizzard Entertainment. Diablo is a trademark of Blizzard Entertainment, Inc. This repository contains no game files or game code dumps; use it only with your own legally owned copy of the game, offline.

## License

[MIT](LICENSE) © 2026 CloneAtWork
