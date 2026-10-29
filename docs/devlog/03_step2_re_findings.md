# Step 2 – Static RE of the D2R 3.3.0 (build 93847) runtime image

Date: 2026-10-21 · Input: `d2r_image_93847.bin` (0x5643000 bytes, sha256 `b5615ca5…0018`), dumped by cowportal-probe 0.1.0.
Tools: `re/d2img.py` (capstone + numpy; .pdata function table, RIP-relative and rel32 xref scans). All RVAs are relative to exeBase `0x140000000`.

## 1. Summary

The game's own cow-portal function was found and fully decoded. It needs only `(pGame, pPlayerUnit)` and does all the checks itself.
A clean hook point that supplies exactly those two pointers on every level change was also found.
**In D2R 3.3.0 the Cow King check is gone**, which matches your report that you have killed him many times in Hell.

## 2. Probe results (Step 1 verification)

| Check | Result |
|---|---|
| Plugin loaded by D2RLoader 1.3.0-beta with ABI 4 | yes (`[PLUGIN] Cow Portal Probe (diagnostic) 0.1.0 by Alex [global]`) |
| contextSize | 200, equal to SDK 0.4.0 |
| Image readable at load | yes: 0x5643000 bytes, zero-filled 0, failed reads 0 |
| `.text` changed between load and LocalPlayerReady | no (FNV identical), so there is no lazy decryption |
| LevelChanged on entering the game | `prev=-1 cur=1`; waypoint out/in gives `1→3`, `3→1` |
| `difficulty` field on Level/Act events | **always 0 even in Hell**. Per the SDK it is only filled for `QuestCompleted`, so the plugin must not use it |
| Errors in d2rloader.log | Shutdown-only `BC_ASSERT` leak asserts (Stats pool / object pool) at Save & Exit. Their stacks contain D2RCore + D2R code only, and the probe does nothing at shutdown. Treated as unrelated pre-existing asserts (not proven) |

## 3. Located functions (evidence)

| Name (D2MOO equivalent) | RVA | How it was found | Expected bytes (verified in dump) |
|---|---|---|---|
| Cube output-type parser (D2Common `HoradricCube.cpp`) | `0x3E5E60` (func) | only xref to string `"Cow Portal"` @ `0x1D0E510`; writes `byte [out+0xD] = 1` | – |
| Cube output dispatch in `PLRTRADE_CreateCubeOutputs` | `0x526D3C` | `type-1 <= 3`, then `call [base + type*8 + 0x1D3BFC0]` with `(rcx=pGame, rdx=pUnit, r8=out+0x5A)` | – |
| Cube output function table | `0x1D3BFC0` | `[0]=0, [1]=0x52A9D0, [2]=0x52A9F0, [3]=0x52A9E0, [4]=0x52AA00` | `00×8, D0 A9 52 40 01 00 00 00 …` |
| `PLRTRADE_CreateCowPortal` (thunk, type 1) | `0x52A9D0` | table[1] | `E9 CB F8 0A 00` → jmp `0x5DA2A0` |
| **`ACT1Q4_CreateCowPortal`** | **`0x5DA2A0`** (to `0x5DA42F`) | target of the thunk, decoded below | `48 89 5C 24 08 55 56 57 48 83 EC 50 48 8B FA 48 8B D9 BA 04 00 00 00 44 8D 42 07 E8 10 AD F3 FF` |
| **`QUESTS_ChangeLevel`** | **`0x5155C0`** (to `0x515635`) | builds a quest-arg struct with event `3` (QUESTEVENT_CHANGEDLEVEL) and walks `pGame+0x21D8` callbacks `[+0x270+3*8]`. Called from `0x486ACF` (same code as D2MOO `Clients.cpp`) and `0x58FA06` | `40 53 48 83 EC 60 41 BA 03 00 00 00 48 89 4C 24 20 44 89 54 24 28 89 54` |
| `QUESTS_GetGlobalState(pGame, quest, flag)` | `0x514FD0` | reads `pGame+0x21D8 → +0x10`, tail-jumps to GetQuestState | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48` |
| `QUESTRECORD_GetQuestState(flags, quest, flag)` | `0x325C50` | bit index `quest*16+flag`, returns 0/1 | `48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 20 41` |
| `UNITS_GetPlayerData(pUnit)` | `0x34B240` | asserts `unit->type == 0`, returns `[unit+0x10]` | `40 53 48 83 EC 20 48 8B D9 48 85 C9 75 13 88 4C` |
| `SUNIT_AttachSound(pUnit, id, pUnit)` | `0x491960` | called on the failure path with id `0x15` | `48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 20 49` |

Cross-check of the table: entry [2] jumps to `0x5DA590`, which uses levels `0x85–0x87` (Pandemonium runs) and `0x6D` (Harrogath). Entry [3] jumps to `0x5DA430`, which uses level `0x88` (Uber Tristram). So entry [1] is the cow portal, consistent with D2MOO's `CUBEOP_COWPORTAL = 1`.

None of these functions starts with a jump, so no other loaded plugin (Monster Display) had hooked them when the dump was taken.

## 4. Decoded `ACT1Q4_CreateCowPortal` @ 0x5DA2A0 (build 93847)

```c
// rcx = pGame, rdx = pUnit, r8 = unused (overwritten before any read)
int CreateCowPortal(Game* g, Unit* u, void*) {
    if (QUESTS_GetGlobalState(g, 4 /*A1Q4*/, 11 /*CUSTOM7*/)) goto fail;      // already opened in this game
    uint8_t ver  = *(uint8_t*)(g + 0x106);   // 1 = Classic, 2 = LoD, 3 = RotW
    uint8_t diff = *(uint8_t*)(g + 0x104);   // 0/1/2
    void* qf = *(void**)(UNITS_GetPlayerData(u) + 0x40 + diff*8);
    if (ver == 1) { if (!GetQuestState(qf, 26 /*A4Q2*/, 0 /*REWARDGRANTED*/)) goto fail; }
    else          { if (!GetQuestState(qf, 40 /*A5Q6*/, 0 /*REWARDGRANTED*/)) goto fail; }
    Room* r = UNITS_GetRoom(u);                                   // 0x34B440
    if (!r || DUNGEON_GetLevelIdFromRoom(r) != 1) goto fail;      // 0x2EFC10, must be Rogue Encampment
    UNITS_GetCoords(u, &xy);                                      // 0x34A040
    QUESTS_GetFreePosition(g, r, &xy, 3, 0x400, &freeRoom, 4, 100); // 0x517F90
    if (!freeRoom) goto fail;
    if (!CreatePortalObject(g, u, freeRoom, x, y, 39 /*Moo Moo Farm*/, 0, 60 /*perm. portal*/, 0, 0)) goto fail; // 0x432CE0
    QUESTS_SetGlobalState(g, 4, 11);                              // 0x5181C0
    return 1;
fail:
    SUNIT_AttachSound(u, 0x15, u);  // "I can't" voice line
    return 0;
}
```

Differences from D2MOO (1.10): there is **no Cow King check** (`A1Q4 / CUSTOM6`), the failure sound id is `0x15` instead of 20, the free-position search takes `pGame` first plus an extra argument `100`, and a game-version byte (Classic / LoD / RotW) is used instead of `bExpansion`.

## 5. Interpretation – plugin design for Step 3 (proposal)

1. Inline-hook `QUESTS_ChangeLevel` (0x5155C0). Call the original first, so normal quest callbacks keep running.
2. If `newLevel == 1` and the unit is a player (`*(uint32_t*)u == 0`), run a **silent pre-check** with the game's own helpers:
   - global flag A1Q4/CUSTOM7 not set yet
   - A5Q6 (or A4Q2 in Classic) reward granted for the current difficulty
3. If the pre-check passes, call `ACT1Q4_CreateCowPortal(pGame, u, nullptr)`. That is exactly what the cube does, so the portal stays open for the rest of the game, and its global flag stops a second portal from opening.
4. Locked difficulty: the pre-check fails and nothing happens, with no "I can't" sound.
5. Before installing anything, all expected bytes are verified with `CheckExpectedBytes`. On a mismatch (for example a game patch) the plugin logs it and installs nothing.
6. Flags: `Server | NativeHooks` (gameplay plugin; it will appear in the character's plugin history). Config `cow-portal-in-town.toml`: `enabled`, per-difficulty on/off, `log_diagnostics`.

## 6. Remaining unknowns (to verify in-game in Step 3)
- Whether `QUESTS_ChangeLevel` also fires when a **new game starts directly in the Rogue Encampment** (expected `0 → 1` from caller `0x486ACF` with no previous room, but unproven). Fallback: a second hook on the player-joined event.
- Whether creating the portal during the level transition (before caller `0x4871A0` updates the client's rooms) shows it on the client straight away. Fallback: defer to the next game tick.
- Multiplayer (TCP host): the hook fires for every player. The first qualifying player opens one shared portal, as the cube would.
- All RVAs apply to build 93847 only; any game patch invalidates them. The expected-bytes check keeps the plugin safe and inactive in that case.
