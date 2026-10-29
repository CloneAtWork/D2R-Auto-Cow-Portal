# Changelog

All versions target **D2R 3.3.0 build 93847** with D2RLoader plugin ABI 4.

## [1.0.0] – 2026-10-29
First public release.

The release DLL is built reproducibly from the tagged source. Its code and data are byte-identical to the binary tested in-game on 2026-10-29 (sha256 `406a12cf…4ced`); only the PE header and export timestamps and the preferred image base differ.

### Features
- Opens the cow level portal automatically in the Rogue Encampment, once per game, for every unlocked difficulty (Baal in LoD/RotW, Diablo in Classic).
- Works when a game starts in town, after a cross-act waypoint, and after in-act arrivals.
- Fixed placement relative to the central campfire (default offset 6,9), using the game's free-position search.
- Silent in locked difficulties: no portal, no "I can't" sound.
- Configurable: master switch, per-difficulty switches, anchor (`bonfire` / `player`), offsets, fallback, diagnostics.
- Refuses to load on any other game build, or when any of the 14 functions or 3 layout anchors differs.
- Release zip that D2RMM imports directly.

### Development history (pre-release, tested privately)
| Version | Change |
|---|---|
| 0.1.0 | Hook `QUESTS_ChangeLevel`, silent pre-check, native cube function. Worked on in-act arrivals only. |
| 0.2.0 | Hook `CLIENTS_SpawnPlayer` (game start); fixed placement at the campfire. |
| 0.2.1 | Default offset (6,6) → (6,9) after in-game feedback. |
| 0.3.0 | Portal is scheduled and created from `GAME_UpdateProgress` once the client reports it is in town. |
| 0.3.1 | Frame scan for arrivals that no hook sees (cross-act waypoints). |
| 0.3.2 | Accept a client "in town" event that arrived before scheduling (cross-act portal now after ~0.3 s instead of 10 s). |
| 1.0.0 | Diagnostics off by default; README; release packaging. |

The full log is in [docs/devlog/HISTORY.md](docs/devlog/HISTORY.md).
