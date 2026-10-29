# v0.3.0 – Fix: no visible portal after a cross-act waypoint (start in Act 5 → Rogue Encampment)

Date: 2026-10-29 · Build 93847 · Bug reported by the user, with the log from 2026-10-26.

## 1. Observed
- `cow-portal-in-town.log` (v0.2.0), 26 Oct:
  - 07:38:25 `Player spawned in game (level 109)` — game starts in Harrogath.
  - 07:39:00.986 `Player spawned in game (level 1)` — waypoint Harrogath → Rogue Encampment (act change goes through `CLIENTS_SpawnPlayer`).
  - 07:39:00.989 `bonfire found after 1 game updates (0 ms)` → 07:39:00.990 `Cow portal opened … at (5742,6022)`.
- User: in that game **no portal was visible** after arriving by **waypoint**.
- Comparison with the working cases:
  - game start in Act 1: the bonfire was missing at spawn, and the portal was created **1297 / 1844 ms later**, after loading → visible.
  - in-act waypoint (ChangeLevel path) → visible.

## 2. Interpretation (root cause, inferred)
On a cross-act waypoint the server places the player in the new act while the client is still on the act-loading screen. The Act 1 rooms (and the bonfire) already exist on the server, so v0.2.0 created the portal in the same server update, 4 ms after the spawn. The object exists on the server (the per-game flag was set, so no second attempt), but the client never received or displayed it. In the game-start case the bonfire happened to be missing, which accidentally delayed creation until after the load.

**Not directly proven:** the exact mechanism by which the client drops the object packet during act loading. The fix is therefore built so that it does not depend on that detail.

## 3. Fix
Arrivals no longer create the portal immediately; they only **schedule** it. Creation happens on the server frame loop once the client is really in town:

| Piece | RVA | Evidence |
|---|---|---|
| `GAME_UpdateProgress(game)` hook (per server frame) | `0x406550` | `inc dword [rcx+0x170]` (dwGameFrame), debug-trigger checks 1/2/4 like D2MOO `GAME_UpdateProgress`, `% 1500` tail @ `0x4085E0`; called from the game-slot loop @ `0x4064C1` with `rcx = game` |
| Client signal | SDK `LifecycleService` | `LevelChanged` with `currentValue == 1` (UI thread) → atomic flag + sequence counter; `GameLeft` → drop the schedule |

Rules, evaluated every server frame for the scheduled game:
1. Drop the schedule if: the client left the game, the player left the game, the player left level 1, or the portal is already open (e.g. via the cube).
2. Ready = a **new** client "in town" signal since scheduling, plus **12 frames** (~0.5 s) to settle, **or** a **250-frame** (~10 s) timeout. The timeout covers remote TCP/IP clients and a missing lifecycle service.
3. Then wait for the bonfire, for up to **375 frames** (~15 s) in total, then fall back to the player position (configurable).

Removed: the `runOnGameThread` retry, which is replaced by the frame hook.

## 4. Tests (Wine harness) – 48/48 PASS
Expected bytes of 14 functions and 3 layout anchors were checked against the real dump.
- **B1 – regression of this exact bug.** Game starts in Act 5, then a waypoint to Act 1 with the bonfire already present. No portal for 50 frames while the client is loading; still none 11 frames after the client signal; the portal appears on the 12th frame. Placement, arguments and flag are as before, with no duplicate.
- J1 game start in town · W1 in-act waypoint · T1 no-signal timeout · T2 a stale earlier "in town" signal is ignored · D1 leaving town before creation, then returning · D2 player leaves the game · D3 another game's frames are ignored · L2 locked difficulty · F1/F2/F3 fallbacks and decoys · C1 Classic · K1–K3 config / no lifecycle / disabled · V1 byte mismatch / wrong build.
- Two harness findings fixed during testing:
  - settle off-by-one: the signal is now sampled before the frame is counted;
  - a schedule left over from a previous game at the same game/player addresses: it is now replaced via the GameLeft sequence.

## 5. Unknowns (to verify in-game)
- Does the UI `LevelChanged → 1` fire only after the act has finished loading? Expected yes; the log line `client reports in town after N frames (X ms)` will show it.
- Does a Server-role plugin receive lifecycle events? The startup log line `client signal on` will show it.
- Is ~0.5 s of settle time enough on the user's PC? If not, that will show as an invisible portal while the log says "opened".
