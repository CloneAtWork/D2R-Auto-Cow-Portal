# v0.3.1 – Fix: cross-act waypoint into the Rogue Encampment is not seen by any hook

Date: 2026-10-29 · Build 93847

## 1. Observed (v0.3.0 test log, 29 Oct)
| Time | Log line | What the user did |
|---|---|---|
| 15:56:20.111 | `Player spawned in game (level 1)` → portal scheduled | session 1 starts in the Rogue Encampment |
| 15:56:22.114 / .546 | `client reports in town after 8 frames (2000 ms)` → `Cow portal opened … (5742,6022)` | portal visible (user confirmed) |
| — | *(no line)* | **waypoint Rogue Encampment → Act 5** |
| 15:56:34.703 | d2rloader.log: `BC_ASSERT s_StatsPool.IsEmpty()` (game teardown) | save & quit session 1 |
| 15:56:37.101 | `Player spawned in game (level 109)` → nothing to do | session 2 starts in Harrogath |
| — | *(no line at all)* | **waypoint Harrogath → Rogue Encampment, no portal** |
| 15:57:16 | `D2RCore unloaded` | game closed |

**Confirmed:** a cross-act waypoint calls **neither** `CLIENTS_SpawnPlayer` (0x485350) **nor** `QUESTS_ChangeLevel` (0x5155C0). With `diagnostics = true` every call of either hook writes a line, and there are none for both waypoint trips.

**Correction of docs/07:** the 26 Oct line 07:39:00 `Player spawned in game (level 1)` was therefore most likely a *new game started in Act 1*, not the waypoint. The root cause given in docs/07 (created while the client was still loading) is **not confirmed**. The settle/client-signal logic from v0.3.0 is kept anyway: it is harmless and makes creation independent of load timing.

**Unknown:** which server function performs the cross-act placement. It is not needed for the fix below.

## 2. Fix: safety-net scan in the per-frame hook
Every 5 server frames (~0.2 s), `GAME_UpdateProgress` (0x406550) does the following unless a portal is already scheduled, open, or definitively failed for this game:
1. Iterate the game's players with `SUNIT_IterateUnitsOfType(game, 0, …)` (0x490DE0).
2. For each player in level 1, run the same checks as before, **silently**: difficulty enabled, not opened, Baal/Diablo done.
3. Schedule the first qualifying player through the existing path: client signal (`LevelChanged → 1`) + 12 frames, or the 250-frame timeout, then bonfire + offset placement.

**Additional guard:** if creation fails definitively in a game (no bonfire with fallback off, or the native cube function returns 0), the game is marked "given up". This prevents rescheduling every 0.2 s and avoids a repeated "I can't" sound.

**Cost:** one global-flag check every 5 frames once the portal is open. Before that, at most 8 players × a few native getters.

## 3. Tests (Wine harness) – 55/55 PASS
New regression tests:
- **B2:** the player is moved into Act 1 with **no hook call at all**. Nothing happens while the client loads; the portal is created 12 frames after the client signal; no duplicates from later scans.
- **B3:** the exact user steps across two sessions (start in Act 1 → portal; waypoint to Act 5; save & quit; new session in Act 5; waypoint to Act 1 → portal).
- **B4:** a locked character standing in town for 1000 frames → no schedule, no native call.
- **K1+:** after a give-up in a game, re-entering town does not reschedule.

All earlier scenarios (B1, J1, W1, T1, T2, D1–D3, L2, F1–F3, C1, K1–K3, V1) still pass. DLL sha256 `4ec7b1e52d2916382cf81991b0a4450fa7d395864c52c52261348bf234edd117`.

## 4. Remaining unknowns
- In-game confirmation of the cross-act case. Expected log lines: `[scan] player found in the Rogue Encampment without an arrival event.`, then `[scan] … portal scheduled`, `client reports in town …` and `Cow portal opened …`.
