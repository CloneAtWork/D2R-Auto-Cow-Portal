# Technical notes

All addresses are **RVAs** relative to the D2R image base (`0x140000000`) of **D2R 3.3.0 build 93847** (`3.3.0 RC 1 7/30/2026`), as mapped by D2RLoader 1.3.0-beta. Names in `UPPER_CASE` follow [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) (a reimplementation of D2 1.10), which served as the reference for locating the equivalent D2R code.

## 1. Why native hooks

The D2RLoader Plugin SDK (0.4.0) offers lifecycle events, game-thread callbacks, config, logging and inline hooks. It has **no API** to create objects, read quest flags, or get the server `Game*` / player `Unit*`. The cow portal therefore has to be created through the game's own code.

`D2R.exe` is encrypted on disk (`.text` entropy 8.0). D2RLoader maps a decrypted runtime image at the original base. The read-only probe in `tools/probe` dumped that image: 0x5643000 bytes, no unreadable pages, `.text` unchanged between load and gameplay. All static analysis was done on that dump.

## 2. The game's cow portal (what the cube does)

| Item | RVA | Evidence |
|---|---|---|
| Cube output parser | 0x3E5E60 | The only xref to the string `"Cow Portal"` (0x1D0E510); writes output type `1` |
| Cube output dispatch | 0x526D3C | `call [base + type*8 + 0x1D3BFC0]` with `(game, unit, output)` |
| Output function table | 0x1D3BFC0 | `[1]`→0x52A9D0 (cow), `[2]`→0x52A9F0 (levels 0x85–0x87, Uber runs), `[3]`→0x52A9E0 (level 0x88, Uber Tristram) |
| `ACT1Q4_CreateCowPortal` | 0x5DA2A0 | Target of the thunk at 0x52A9D0 |

Decoded `ACT1Q4_CreateCowPortal(game, unit, unused)`:

```c
if (QUESTS_GetGlobalState(game, A1Q4=4, CUSTOM7=11)) fail;          // already opened in this game
qf = UNITS_GetPlayerData(unit)->questFlags[game->difficulty];      // +0x40 + diff*8, difficulty @ game+0x104
if (game->version == 1 /*Classic, game+0x106*/) { if (!GetQuestState(qf, A4Q2=26, REWARDGRANTED)) fail; }
else                                            { if (!GetQuestState(qf, A5Q6=40, REWARDGRANTED)) fail; }
room = UNITS_GetRoom(unit); if (!room || LevelId(room) != 1) fail; // must be in the Rogue Encampment
UNITS_GetCoords(unit, &xy);
QUESTS_GetFreePosition(game, room, &xy, 3, 0x400, &freeRoom, 4, 100);
if (!freeRoom || !CreatePortalObject(game, unit, freeRoom, x, y, 39 /*Moo Moo Farm*/, 0, 60 /*perm. portal*/, 0, 0)) fail;
QUESTS_SetGlobalState(game, 4, 11); return 1;
fail: SUNIT_AttachSound(unit, 0x15, unit); return 0;                // the "I can't" voice line
```

Compared with D2 1.10, **there is no Cow King check** (A1Q4/CUSTOM6) any more. Players can kill him without losing access.

## 3. Functions used by the plugin

| Function | RVA | Use |
|---|---|---|
| `GAME_UpdateProgress(game)` | 0x406550 | **hook**: per server frame. Identified by `inc [game+0x170]` (frame counter), the debug-trigger checks and the `% 1500` tail; called from the game-slot loop at 0x4064C1 |
| `QUESTS_ChangeLevel(game, old, new, unit)` | 0x5155C0 | **hook**: level change within an act. Builds quest event 3 (CHANGEDLEVEL); called from 0x486ACF (`Clients.cpp`) and 0x58FA06 |
| `CLIENTS_SpawnPlayer(client, unit, game, room, x, y)` | 0x485350 | **hook**: game join placement. Null asserts, town spawn location, packet 0x15 |
| `QUESTS_GetGlobalState` / `SetGlobalState` | 0x514FD0 / 0x5181C0 | Per-game quest flags at `game+0x21D8 → +0x10` |
| `QUESTRECORD_GetQuestState(flags, quest, flag)` | 0x325C50 | Bit `quest*16 + flag` |
| `UNITS_GetPlayerData(unit)` | 0x34B240 | `unit+0x10`; asserts `unit->type == 0` |
| `UNITS_GetCoords` / `UNITS_GetRoom` | 0x34A040 / 0x34B440 | Via `unit+0x38` (path) |
| `DUNGEON_GetLevelIdFromRoom` | 0x2EFC10 | Leaf function |
| `QUESTS_GetFreePosition` | 0x517F90 | Spiral search, crosses into adjacent rooms |
| `CreatePortalObject` | 0x432CE0 | Town rooms only accept destinations 39 and 133–137 with object 60 |
| `SUNIT_IterateUnitsOfType(game, type, arg, cb)` | 0x490DE0 | Player list at `game+0x2230` |
| `ACT1Q4_CreateCowPortal` | 0x5DA2A0 | Used only for `anchor = "player"` and as the fallback |

Layout facts, each also verified as expected bytes at load: object hash list `game+0x2A30` (128 buckets; `lea rsi,[rcx+0x2A30]` @ 0x510E8D), unit list next `+0x158` (@ 0x490031), class id `unit+4` (@ 0x349886). The Rogue bonfire is object class **39** (objects.txt `RogueBonfire`).

## 4. Design

### Arrival detection: three sources

1. `CLIENTS_SpawnPlayer`: game start. `QUESTS_ChangeLevel` does **not** fire on join (observed).
2. `QUESTS_ChangeLevel`: walking or waypoint inside an act.
3. A **scan every 5 server frames** in `GAME_UpdateProgress`: is any qualifying player standing in level 1? A **cross-act waypoint calls neither hook** (observed: no log line for either direction), so this scan is what catches it.

### Timing: wait for the client

Arrivals only *schedule* the portal; it is created when the client is really in town. This was introduced as a precaution, because an object created while the client is still on the loading screen might never be shown to it. The original "missing portal" bug turned out to be undetected cross-act arrivals (see above), but the wait is kept because it makes creation independent of load times. The portal is created once:
- the local client reports `LevelChanged → 1` (SDK `LifecycleService`, UI thread, passed over through atomics), either after scheduling **or already before it**: on a cross-act waypoint the client event arrives ~3 ms *before* the scan sees the player (observed); and
- 12 more server frames have passed (~0.5 s settle).

If no client event arrives (a remote TCP/IP player, or no lifecycle service), the portal is created after 250 frames (~10 s). The campfire must exist (town rooms populate lazily); the plugin waits up to 375 frames, then falls back to the player position. The schedule is dropped on client game-leave, when the player leaves the game or town, or when the portal already exists. A definitive failure marks the game "given up", so there are no retries every frame and no repeated "I can't" sound.

### Placement

`campfire coords + (offset_x, offset_y)` → `QUESTS_GetFreePosition(game, campfireRoom, &xy, 3, 0x400, &room, 4, 100)` → `CreatePortalObject(game, player, room, x, y, 39, null, 60, 0, 0)` → `QUESTS_SetGlobalState(game, 4, 11)`. This is exactly what the cube does, only at a different position.

### Safety

- The plugin refuses to load unless `buildName == "93847"` and all 14 functions plus 3 layout anchors match their expected bytes (`CheckExpectedBytes`).
- The original function always runs first in every hook; the plugin never modifies game code apart from the three hook entries.
- `UNITS_GetPlayerData` is only called for `unit->type == 0`, because it asserts otherwise.

### Measured timings (in-game logs, 29 Oct 2026)

| Case | Arrival → portal |
|---|---|
| Game start in the Rogue Encampment | 1.8 s after the server spawn (≈1.4 s of loading screen + 0.45 s settle) |
| Cross-act waypoint Harrogath → Rogue Encampment | 0.31 s |

## 5. Unknowns

- Which server function places the player on a cross-act waypoint. It was not needed, because the frame scan covers it.
- Whether the default offset is ideal in all four town layout variants. It was confirmed in the tested games.
