# v0.2.0 – Portal on game start + fixed placement at the bonfire

Date: 2026-10-21 · Build 93847 · Input: user test of v0.1.0 + static RE of `d2r_image_93847.bin`

## 1. Test result of v0.1.0 (observed)

`cow-portal-in-town.log`, 16:45–16:47:
- Hook installed. Portals opened on arrival in Hell and Normal (`Cow portal opened … (Hell)`, `(Normal)`).
- A locked character produced `Arrival 2->1 (Normal, version 3)` and `Arrival 3->1 (Hell, version 3)` with "not unlocked", and no portal. This confirms the silent pre-check works. **version 3 = RotW**, which uses A5Q6.
- **No log line at all when a game started directly in the Rogue Encampment.** So `QUESTS_ChangeLevel` is *not* called on game join, which confirms the open question from Step 2.
- d2rloader.log shows only the same shutdown `BC_ASSERT` leak messages as before the plugin existed.

## 2. New hook point: `CLIENTS_SpawnPlayer` @ 0x485350

| Evidence | Detail |
|---|---|
| Signature | `(rcx=pClient, rdx=pPlayer, r8=pGame, r9=pRoom, [x], [y])`, three null asserts. Matches D2MOO `sub_6FC31EF0` (Clients.cpp), which GAME_SendActInit calls right after `CLIENTS_AddPlayerToGame` |
| Body | if `pRoom == 0`, it finds the act's town spawn location (uses `game+0x106`), sends the level packet, calls the client room update `0x4871A0`, places the unit (`0x48EE10`), then sends packet `0x15` (`mov dl, 0x15` @ 0x485543) |
| Callers | `0x484330` and `0x486750` (act-change paths, both pass `client+0x278` = player, `client+0x2B0` = game), plus the join path |
| Expected bytes | `40 55 53 41 54 41 55 41 57 48 8D 6C 24 D9 48 81 EC B0 00 00 00 48 8B 05 5C 5F 54 02 48 33 C4 48 89 45 FF 49` |

The plugin calls the original first, then checks whether the player is now in level 1.

## 3. Fixed placement: anchor = Rogue bonfire

**Why the bonfire:** `lvlprest.txt` row "Act 1 - Town 1" has **4 layout files**: `TownN1/TownE1/TownS1/TownW1.ds1`. Absolute coordinates therefore change from game to game. The central `RogueBonfire` (objects.txt ID **39**, objpreset act 1 index 2) is the one object that marks the camp centre in every layout.

| Item | RVA / offset | Evidence |
|---|---|---|
| Object hash list | `game+0x2A30`, 128 buckets | `0x510E8D lea rsi,[rcx+0x2A30]`, loop of 0x80. The stride 0x400 matches type 0 at `+0x2230` and type 1 at `+0x2630` (the type switch at `0x51BCD6`/`0x51BD21`/`0x51BD3C`) |
| List next | `unit+0x158` | `0x490031 mov rax,[rbx+0x158]` (get-next helper used by the list walkers) |
| Class id | `unit+0x04` | `0x349886 mov eax,[rcx+4]`, an accessor that returns -1 for null |
| UNITS_GetCoords | `0x34A040` | `(unit, Coord*)`; static path `+0x10/+0x14` for objects |
| UNITS_GetRoom | `0x34B440` | `(unit)`, used by the cow function |
| DUNGEON_GetLevelIdFromRoom | `0x2EFC10` | leaf function `test rcx; mov rcx,[rcx+0x18]; jmp` |
| QUESTS_GetFreePosition | `0x517F90` | `(game, room, &xy, 3, 0x400, &outRoom, 4, 100)`, the same call as the cube. Spiral search; crosses into neighbouring rooms like D2MOO |
| CreatePortalObject | `0x432CE0` | `(game, unit, room, x, y, 39, null, 60, 0, 0)`. Town rooms only accept destinations 39 / 133–137 with object 60 (`0x432D6B`–`0x432D85`) |
| QUESTS_SetGlobalState | `0x5181C0` | `(game, 4, 11)`, the same per-game flag the cube sets |
| SUNIT_IterateUnitsOfType | `0x490DE0` | `(game, type, arg, cb)`; type 0 uses list `+0x2230`. Used only to check that a player is still in the game during retries |

Placement = bonfire position + `offset_x/offset_y` from the config (default `6,6`), then the game's own free-position search. Distances are in sub-tiles, and the camp interior is assumed to be the same in all 4 layouts (**unverified**).

The bonfire search checks `type == 2`, `classId == 39` **and** that the object's level is 1. If the list offset were ever wrong, it simply finds nothing; it never misreads a wrong object.

**Retry:** if the bonfire does not exist yet when the player spawns (rooms not populated), the plugin queues `runOnGameThread` again, up to 150 times. Each retry checks that the player is still in the game. When the retries run out it falls back to opening the portal next to the player (`fallback_to_player`). The log reports the attempt count and elapsed ms, which shows the real queue timing in-game.

## 4. Tests (harness, Wine 9.0) – 33/33 PASS
Expected bytes for all 13 functions and the 3 layout anchors were checked against the real dump. Scenarios:
1. Game start in town → portal at bonfire+offset, via the free spot the game returned; args `(39, null, 60, 0, 0)`; flag A1Q4/CUSTOM7 set.
2. Waypoint out and back → no second portal.
3. Start in Act 2 → nothing; later arrival 40→1 → portal.
4. Bonfire appears after 3 updates → portal once it exists.
5. Bonfire never appears → falls back to the player position after 150 updates.
6. Player leaves during a retry → retry dropped.
7. Locked difficulty → nothing, no retry, no native call.
8. Decoy objects (class 39 in level 2, waypoint 119) → ignored.
9. No free position → fallback.
10. Classic + A4Q2 → portal.
11. Config: anchor=player, negative offsets, invalid values (warning + default), fallback off, enabled=false.
12. Anchor-byte mismatch or wrong build → load fails, no hooks.

## 5. Unknowns
- Whether the bonfire object already exists at spawn time. The log will say (`bonfire found after N game updates`).
- Whether `runOnGameThread` re-queued from inside a game callback runs on the *next* update. If it runs in the same update, the log shows `150 attempts (0 ms)`.
- Whether offset (6,6) is "central but not in the way". The log prints bonfire, requested and final coordinates for tuning.
- Whether the camp interior is identical in all 4 town layouts.
