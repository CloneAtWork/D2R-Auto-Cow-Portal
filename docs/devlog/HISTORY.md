# HISTORY – D2R Cow Portal in Town (D2RLoader plugin)

## 2026-10-21 16:05 - Investigation of plugin feasibility

### Files Changed
- `docs/01_investigation_cow_portal_plugin.md` (new)
- `docs/SDK_COMMIT.txt` (new) – pinned PluginSDK commit `03a5af9e0feb3f95f95e148e330e01f6635950df`

### Summary
Inventoried both connected folders (read-only), found the official D2RLoader PluginSDK, and confirmed that the cow-portal logic exists as one self-contained function in D2MOO (`ACT1Q4_CreateCowPortal`).

### Reason
Find out whether, and how, a D2RLoader plugin can open the cow portal automatically.

### Tests
- pefile: exports/imports/resources of the 4 existing plugins. All export the 3 `D2RLoader*` symbols; manifest RCDATA 1001 = ABI 4 (Monster Display = ABI 2).
- pefile: `D2R.exe` `.text` entropy = 8.0, i.e. encrypted on disk.
- MinGW-w64 GCC 13 compiles the SDK `hello-console` example.

### Result
Feasible, but only with native hooks plus RVAs taken from the decrypted in-memory image. The SDK has no portal or quest API.

### Remaining Questions
- Does D2R 3.3.0 still contain `ACT1Q4_CreateCowPortal` and the Cow King check?
- Does D2RLoader 1.3.0-beta accept plugin ABI 4?

## 2026-10-21 16:17 - Step 1: diagnostic plugin cowportal-probe 0.1.0

### Files Changed
- `probe/cowportal_probe.cpp` (new)
- `probe/cowportal_probe.rc` (new) – manifest resource 1001 = ABI 4
- `probe/build.sh` (new) – MinGW cross-build, fully static (imports only KERNEL32 + msvcrt)
- `probe/test_harness.cpp` (new) – host simulator for testing outside the game

### Summary
Read-only plugin with role `Client` and no hooks or patches. It:
- logs context and ABI
- logs the in-memory PE sections with FNV-1a hashes
- dumps `[exeBase, exeBase+extent)` once per build to `d2rloader\logs\cowportal-probe\d2r_image_<build>.bin`, with a `.txt` region map next to it
- logs lifecycle events
- re-hashes the sections on `LocalPlayerReady`

### Reason
`D2R.exe` is encrypted on disk. The decrypted image D2RLoader maps is needed for static RE in Step 2. The event logs confirm how `LevelChanged` / `difficulty` / `QuestCompleted` behave in 3.3.0.

### Tests
Wine 9.0 + `test_harness.exe`. Fake host, with exeBase = the harness exe itself. 13 checks, all PASS:
- exports resolve; PluginInfo id, ABI and role are correct; load returns true
- dump exists and is ≥ SizeOfImage; starts with `MZ`; the first section matches live memory byte-for-byte (RVA layout preserved)
- sidecar exists; 6 listeners registered; events are logged
- second load: returns true and leaves the existing dump untouched

DLL sha256 `de4ece9c1fea55e77319f1c690b1618a338249136b7ae4888c872d9af1861824`.

### Result
Works in the simulated host. **Not yet tested inside D2R/D2RLoader.**

### Remaining Questions
- Will D2RLoader 1.3.0-beta load an ABI 4 plugin? If not, rebuild with ABI 3.
- Are all pages of the runtime image readable at plugin load time? The sidecar `zeroFilled` / `failedReads` values will show this.

## 2026-10-21 16:35 - Step 1 verified in-game + Step 2 static RE of build 93847

### Files Changed
- `docs/03_step2_re_findings.md` (new)
- `re/d2img.py` (new) – deterministic static-analysis helpers (.pdata function table, xref scans, disassembly)

### Summary
- The probe ran in the real game. ABI 4 is accepted, all 0x5643000 bytes were dumped with 0 failed reads, and `.text` does not change at runtime.
- Located and decoded:
  - `ACT1Q4_CreateCowPortal` @ `0x5DA2A0`, through the cube output table @ `0x1D3BFC0`, entry 1
  - `QUESTS_ChangeLevel` @ `0x5155C0`
  - helpers `GetGlobalState` `0x514FD0`, `GetQuestState` `0x325C50`, `GetPlayerData` `0x34B240`, `AttachSound` `0x491960`
- Expected bytes recorded for each one.

### Reason
These RVAs are needed for the Step 3 plugin: the hook point plus a silent pre-check plus a call to the native portal function.

### Tests
- `"Cow Portal"` string → parser writes type 1 → dispatch `call [table + type*8]` → table[1] thunk → `0x5DA2A0`. Table entries 2 and 3 cross-checked as the Uber portals (levels 0x85–0x88, Harrogath 0x6D).
- `QUESTS_ChangeLevel` caller at `0x486ACF` matches D2MOO `Clients.cpp` instruction for instruction.
- Quest ids and flags in the decoded code match D2MOO: A1Q4 = 4, CUSTOM7 = 11, A4Q2 = 26, A5Q6 = 40, MooMoo = 39, portal object = 60.

### Result
- The cow portal function in 3.3.0 has **no Cow King check**, which is consistent with the user killing the Cow King repeatedly in Hell.
- LevelChanged lifecycle events always report `difficulty = 0`, so that field is unusable for this plugin.

### Remaining Questions
- Does ChangeLevel fire when a game starts directly in the Rogue Encampment?
- Does a portal created mid-transition appear on the client immediately?
- `game+0x106 == 1` meaning Classic is inferred from the SDK `GameVersionClassic = 1` and from game logic, not proven directly.

## 2026-10-21 16:42 - Step 3: plugin cow-portal-in-town 0.1.0

### Files Changed
- `plugin/cow_portal_in_town.cpp` (new)
- `plugin/cow_portal_in_town.rc` (new) – resource 1001 = ABI 4, resource 1002 = embedded default config
- `plugin/cow_portal_in_town.toml` (new) – default config
- `plugin/build.sh` (new) – MinGW static build
- `plugin/test_harness.cpp` (new) – host and game simulator
- `docs/04_step3_install_and_test.md` (new)

### Summary
A Server + NativeHooks plugin. It inline-hooks `QUESTS_ChangeLevel` (0x5155C0) and always runs the original first.

On arrival in level 1 (Rogue Encampment) with a player unit:
1. Silent pre-check:
   - difficulty enabled in config
   - not already opened (A1Q4 / CUSTOM7)
   - A5Q6 reward granted (A4Q2 for Classic)
2. Then it calls the native `ACT1Q4_CreateCowPortal(game, unit, nullptr)` (0x5DA2A0).

Before installing anything it checks buildName `93847` and the expected bytes of all 5 functions; on a mismatch the load fails cleanly. Config: `enabled`, `normal` / `nightmare` / `hell`, `diagnostics`.

### Reason
Implements the user's goal: instant cow portal in every unlocked difficulty, without Wirt's Leg.

### Tests
Wine 9.0 + `plugin_harness.exe`, **27/27 PASS**. Native calls go through jump stubs at the real RVAs, and expected bytes are checked against the real dump. Covered:
- load
- LoD Hell first arrival (native called once)
- second arrival (no call)
- other level (no checks)
- locked difficulty (silent, no native call, so no sound)
- Classic A4Q2 path
- non-player unit ignored
- old level 0 / -1
- native returning 0, then retry
- config: BOM / CRLF / comments, hell = false, enabled = false (no hook), malformed value
- wrong build and byte mismatch (no hook)

DLL sha256 `fdd48fee8ea4bee2ff5c6d43d677ffdc9fbcbe23a0f5e73d93bb593a6c1469da`.

### Result
Logic verified in simulation. **Not yet tested in the real game.**

### Remaining Questions
- In-game: does a new game started in Act 1 trigger the hook (old level 0)?
- Is the portal visible immediately?
- Is there any conflict with Monster Display or the other plugins?
- Server role means the plugin is recorded in the character's plugin history.

## 2026-10-21 17:02 - v0.2.0: portal on game start + fixed placement at the bonfire

### Files Changed
- `plugin/cow_portal_in_town.cpp` (rewritten for 0.2.0)
- `plugin/cow_portal_in_town.toml` (new `[placement]` section)
- `plugin/test_harness.cpp` (rewritten: fake game object list, retry queue)
- `docs/05_v020_join_hook_and_placement.md` (new)
- `docs/06_v020_install_and_test.md` (new)

### Summary
- **New hook `CLIENTS_SpawnPlayer` @ 0x485350.** It runs on game join and act spawn, so the portal now also opens when a character loads directly into the Rogue Encampment.
- **Fixed placement.** The plugin finds the RogueBonfire (object 39) in the game object list (`game+0x2A30`, next pointer `+0x158`, class id `+4`). It adds the configurable offset (default 6,6), runs the game's free-position search (0x517F90), then calls `CreatePortalObject` (0x432CE0) with the cube's arguments and sets A1Q4/CUSTOM7 via 0x5181C0.
- **Retry.** If the bonfire is not spawned yet, the attempt is retried through `runOnGameThread` (up to 150 times), then falls back to the player position (configurable).
- **Config.** `anchor` = bonfire | player, `offset_x`, `offset_y`, `fallback_to_player`.
- **Load-time checks.** Expected bytes now cover 13 functions plus 3 layout anchors.

### Reason
User test of v0.1.0 found two problems:
- No portal when a game starts directly in the encampment. The log confirms ChangeLevel does not fire on join.
- The user wants a fixed, central spot instead of next to the player.

The locked-difficulty behaviour was confirmed correct.

### Tests
Wine harness, **33/33 PASS** (scenarios listed in docs/05 §4). DLL sha256 `eed7f758e172310b9252a2c7e29874f74499c4ad69527f0772530db4eca95193`.

### Result
Logic verified in simulation. **Not yet tested in-game.**

### Remaining Questions
- Does the bonfire exist at spawn time, or are retries needed?
- Is the runOnGameThread re-queue timing right? (the log prints attempts and ms)
- Is the default offset a good "central but not in the way" spot in all 4 town layouts?

## 2026-10-21 18:18 - v0.2.1: portal moved slightly lower-left (default offset_y 6 -> 9)

### Files Changed
- `plugin/cow_portal_in_town.cpp` (version 0.2.1, default `offset_y = 9`)
- `plugin/cow_portal_in_town.toml` (default `offset_y = 9`)
- `plugin/test_harness.cpp` (expectations updated)
- User config `d2rloader\config\cow-portal-in-town.toml`: `offset_y` 6 -> 9 (the only change)

### Summary
Default offset from the bonfire changed from (6,6) to (6,9). +y is down-left on screen, so the portal moves 3 sub-tiles toward the lower left.

### Reason
v0.2.0 confirmed working in-game. The user asked for the portal to sit "a bit more to the lower left".

### Tests
- **In-game log of v0.2.0, Hell:**
  - On spawn the bonfire did not exist yet. It was found on the first retry, **1297 ms** later (the next game update after loading), so the retry mechanism is required and works.
  - Portal at (5742,6019): bonfire (5737,6012), requested (5743,6018). The free-position search moved it by (-1,+1).
  - Waypoint back to town gave "already opened", so no duplicate.
- **Harness:** 33/33 PASS. DLL sha256 `612bdd2402dbad4b1162bedad1791974e04c15168d5b5e681720978d2b64460d`.

### Result
- The user confirmed in-game: portal on load, a single portal per game, a good central position, and no portal for locked difficulties.
- The 0.2.1 position is not yet confirmed in-game.

### Remaining Questions
- Is the position good in all 4 town layouts? Only the layout(s) in the user's test have been seen so far.
- Should diagnostics be off by default for a final release?

## 2026-10-21 18:31 - v0.2.1 position confirmed in-game

### Files Changed
- none (documentation only)

### Summary
User confirmed that the portal position with offset (6,9) from the bonfire looks good.

### Reason
Close the open question from the 18:18 entry.

### Tests
User test in-game (manual).

### Result
v0.2.1 is functionally complete. Confirmed by the user in-game:
- portal on game load
- portal on waypoint arrival
- one portal per game
- position good
- no portal for locked difficulties

### Remaining Questions
- Is the position good in all 4 town layouts? Not systematically checked; the user reported "looks good".
- Release polish (proposed, not done): diagnostics off by default, README, distributable package (zip / D2RMM import), optional Git repo.
- After any D2R patch the plugin disables itself and needs a re-dump with cowportal-probe plus new RVAs.

## 2026-10-29 15:53 - v0.3.0: fix invisible portal after cross-act waypoint (session started in another act)

### Files Changed
- `plugin/cow_portal_in_town.cpp` (0.3.0: schedule + per-frame creation, lifecycle client signal, retry queue removed)
- `plugin/test_harness.cpp` (rewritten: server frames, UI lifecycle events, 48 checks)
- `docs/07_v030_cross_act_fix.md` (new)

### Summary
- New hook `GAME_UpdateProgress` @ 0x406550 (per server frame).
- Arrivals (spawn and ChangeLevel) now only schedule the portal. It is created when:
  - the local client reports `LevelChanged -> 1` (SDK lifecycle, UI thread) plus 12 frames, or
  - after a 250-frame timeout,
  - and in both cases only once the bonfire exists (bonfire wait up to 375 frames, then fallback).
- The schedule is dropped on game leave, when the player leaves town, or when the portal is already open.

### Reason
- User report: start in Act 5, waypoint to the Rogue Encampment, no portal.
- Log 26 Oct 07:39:00: the portal was created 4 ms after the cross-act spawn, while the client was still loading the act. The object existed on the server (per-game flag set) but was never shown.

### Tests
Wine harness **48/48 PASS**, including regression test B1 for this bug. DLL sha256 `ae13627c253e03ea713ce176d1ab20c353fc5f1104a22ccbd84e1e1527abf0b0`.

### Result
Fix verified in simulation. **Not yet verified in-game.**

### Remaining Questions
- Is the UI LevelChanged timing after an act load correct?
- Are lifecycle events delivered to a Server-role plugin?
- Is 0.5 s of settle time enough?

## 2026-10-29 16:01 - v0.3.1: safety-net scan for arrivals no hook sees (cross-act waypoint)

### Files Changed
- `plugin/cow_portal_in_town.cpp` (0.3.1: per-frame scan, silent qualification, give-up marker)
- `plugin/test_harness.cpp` (regression tests B2–B4 and the give-up test; 55 checks)
- `docs/08_v031_scan_safety_net.md` (new; also corrects the root cause stated in docs/07)

### Summary
Every 5 server frames, `GAME_UpdateProgress` (0x406550) looks for a qualifying player in level 1. If one is found and no portal is open or scheduled, it schedules one through the existing client-signal + settle + bonfire logic. A per-game give-up marker prevents repeated attempts and sound spam after a definitive failure.

### Reason
v0.3.0 test log (29 Oct 15:56–15:57): neither the waypoint Act 1 → Act 5 nor the waypoint Act 5 → Act 1 produced any hook call. So cross-act waypoints bypass both `CLIENTS_SpawnPlayer` and `QUESTS_ChangeLevel`, and the user saw no portal in session 2.

### Tests
Wine harness **55/55 PASS**. DLL sha256 `4ec7b1e52d2916382cf81991b0a4450fa7d395864c52c52261348bf234edd117`.

### Result
Fix verified in simulation. **Not yet verified in-game.**

### Remaining Questions
- In-game confirmation of the cross-act waypoint case.
- The server function used for cross-act placement is still unidentified; the fix does not need it.

## 2026-10-29 16:07 - v0.3.2: cross-act portal appears ~0.5 s after arrival instead of ~10 s

### Files Changed
- `plugin/cow_portal_in_town.cpp` (0.3.2: readiness also accepts an "in town" client signal received *before* scheduling; logs client level changes)
- `plugin/test_harness.cpp` (new regression test B6; 57 checks)

### Summary
- The user confirmed v0.3.1 works: the portal spawns after a cross-act waypoint, just a few seconds late.
- Log 16:04:44.426 `[scan] portal scheduled`, then 16:04:54.266 `no client signal after 250 frames (9844 ms); creating anyway`, so the portal came from the 10 s timeout.
- Inferred cause: on a cross-act waypoint the client's `LevelChanged -> 1` arrives *before* the server-side scan sees the player in level 1. v0.3.0/0.3.1 only accepted a signal newer than the schedule and so waited for the timeout.
- Fix: also ready if the client's latest level is already 1 at scheduling time, followed by the normal 12-frame settle.
- New diagnostic lines `[client] level A -> B.` make the order of events visible.

### Reason
The user asked for the portal to appear promptly ("takes a few seconds").

### Tests
Wine harness **57/57 PASS**. B6 reproduces the order from the log (client signal first, then the server sees the player) and confirms creation within 18 frames, not 250. DLL sha256 `0d65f4e3124dc0488dcc0d213fdf1462dd18dd2fac3c8a4c7ea35dbf24588644`.

### Result
Verified in simulation. **Not yet verified in-game.**

### Remaining Questions
- The event order is inferred, not observed; the new `[client]` log lines will confirm it.

## 2026-10-29 16:09 - v0.3.2 confirmed in-game by the user

### Files Changed
- none (documentation only)

### Summary
The user confirmed the behaviour is "exactly how it should be". The v0.3.2 log (29 Oct 16:08–16:09) shows:
- **Session start in the Rogue Encampment:** spawn 16:08:41.889, then `[client] level -1 -> 1` at 16:08:43.251, then portal at 16:08:43.721 (1828 ms after spawn; most of that is the loading screen).
- **Cross-act waypoint Harrogath → Rogue Encampment (new session started in Act 5):**
  - `[client] level 109 -> 1` at **16:09:04.834**
  - `[scan] player found` at **16:09:04.837**, so the client event came *before* the server-side scan. This confirms the inference from the 16:07 entry.
  - Portal at 16:09:05.154 (**313 ms** after arrival)
- Waypoint Act 1 → Act 5 produced only a client event and no server hook call. This confirms that cross-act transitions bypass both hooks.

### Reason
Close the verification of v0.3.2.

### Tests
In-game manual test by the user, plus the log above.

### Result
v0.3.2 is functionally complete and confirmed in-game for:
- start in town
- cross-act waypoint
- in-act waypoint (earlier versions, same path)
- one portal per game
- position
- locked difficulties

### Remaining Questions
- Release polish (not done): diagnostics off by default, README, distributable package.
- Any D2R patch requires a new dump and new RVAs; the plugin disables itself safely until then.

## 2026-10-29 16:13 - Release 1.0.0: diagnostics off by default, README, D2RMM-importable package

### Files Changed
- `plugin/cow_portal_in_town.cpp` (version 1.0.0; default `[diagnostics] enabled = false`)
- `plugin/cow_portal_in_town.toml` (same default + comment that startup, warning and error lines are always logged)
- `plugin/test_harness.cpp` (default-diagnostics check; K4 CRLF config test; 61 checks)
- `release/README.md` (new)
- `release/pkg/` (new):
  - `cow-portal-in-town-1.0.0.zip`
  - `cow-portal-in-town-1.0.0-d2rmm.zip`
  - `README.md`
  - `SHA256SUMS.txt`

### Summary
- **Code.** No functional change against 0.3.2 except the diagnostics default.
- **README.** What it does, requirements, D2RMM and manual installation, load check, config with on-screen offset directions, limitations, troubleshooting, uninstall, and a short "how it works".
- **D2RMM zip.** Layout `plugins/cow-portal-in-town.dll` + `config/cow-portal-in-town.toml`, the same layout as RuffnecKk-mass-identify, which D2RMM imported as `plugins\…` / `config\…`.
- **Full zip.** Contains the README, the D2RMM zip and `manual/plugins` + `manual/config`.
- Text files use CRLF. Zip timestamps are fixed, so the build is reproducible.

### Reason
User asked for items 1–3 of the release polish list.

### Tests
- Wine harness **61/61 PASS**, including the packaged CRLF config.
- DLL: exports the 3 D2RLoader symbols, imports KERNEL32 + msvcrt, resource 1001 = 04000000, resource 1002 = default TOML with `enabled = false`.
- Zips pass `testzip()`; the DLL in both zips is byte-identical to the build.
- sha256:
  - DLL `406a12cfa5bf34bc3db177fc7958126d67345504902dbef83a9b5beec8444ced`
  - d2rmm zip `6212678092393d93452eb7a14f042b92a2d7391ca0d4d8bbd411af9208f8962c`
  - full zip `2e4ea1a84598ad6568f5e491f43552468a575e35ab7af6e11e27dffca83feb89`

### Result
Release package built. **D2RMM import of the zip not yet tested by the user.**

### Remaining Questions
- Does D2RMM import the zip as expected? Expected manifest entries: `plugins\cow-portal-in-town.dll` (role plugin) and `config\cow-portal-in-town.toml` (role config).
- Does D2RMM overwrite a user-edited config on reinstall? Unverified; noted in the README.
- The user's current live config `d2rloader\config\cow-portal-in-town.toml` still has diagnostics on; not changed.

## 2026-10-29 16:22 - Release 1.0.0 confirmed: D2RMM import + in-game

### Files Changed
- none (documentation only)

### Summary
- The user confirmed that 1.0.0 works after installation via D2RMM.
- D2RMM import manifest `D:\D2RMM Custom 1.9.9\d2rloader\cow-portal-in-town-1.0.0-d2rmm\manifest.json` (imported 2026-10-29T15:18:34Z):
  - `plugins\cow-portal-in-town.dll`: role plugin, sha256 `406a12cf…4ced`, identical to the release DLL
  - `config\cow-portal-in-town.toml`: role config, sha256 `6696e094…855d`, identical to the packaged CRLF config
  - **warnings: none**

### Reason
Close the open questions from the release entry.

### Tests
- The manifest hashes were compared with the build artifacts: both match.
- User in-game test: confirmed working.

### Result
**Project goal achieved.** In every unlocked difficulty the cow portal opens automatically in the Rogue Encampment, at a fixed spot near the campfire:
- when a game starts there,
- on a waypoint arrival from the same act,
- on a waypoint arrival from another act.

The package installs cleanly through D2RMM.

### Remaining Questions
- Whether D2RMM overwrites a user-edited config on reinstall is still unverified (noted in the README).
- Future D2R patches need a new dump (cowportal-probe), new RVAs (re/d2img.py) and a rebuild. The plugin disables itself safely until then.
