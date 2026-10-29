# D2R Cow Portal in Town – Investigation Report (Step 0)

Date: 2026-10-21 · Status: investigation only, **no code written, nothing changed on your PC**

## 1. Summary

A D2RLoader plugin can do this, but **not with the SDK alone**. The SDK has no "open portal" or "read quest flags" API.
The plugin has to call the game's own cow-portal function (the one the Horadric Cube calls) through native hooks.
To find that function, we first need a decrypted copy of the game code, because `D2R.exe` on disk is encrypted.

Recommended path: 3 steps, with a stop for your confirmation after each one.

| Step | What | Output |
|---|---|---|
| 1 | Diagnostic plugin `cowportal-probe`: dumps the decrypted, in-memory D2R image to a file and logs lifecycle events | `.dll` + dump file you send back |
| 2 | Static RE of the dump: find the cow-portal function, a "player changed level" hook point, and the failure-sound call | RVAs + expected bytes, documented |
| 3 | Real plugin `cow-portal-in-town`: when you enter the Rogue Encampment, opens the portal once per game if the difficulty is unlocked | `.dll` + `.toml` config |

## 2. Observed evidence

### 2.1 Environment (from `d2rloader/logs/d2rloader.log`)
- D2RLoader **v1.3.0-beta**, D2R **3.3.0 build 93847** (`3.3.0 RC 1 7/30/2026`), mod `D2RMM`, `-txt`.
- Log line: `Runtime image mapping: originalBase=0000000140000000 ... imageBytes=65503232`. D2RLoader maps its own decrypted runtime image at the original base, so plugin RVAs refer to that image.
- The log reports that a newer D2RLoader is available.

### 2.2 D2R.exe is encrypted on disk
`D2R.exe` sections (pefile): `.text` VA `0x1000`, size `0x15CBDC0`, **entropy 8.0** (fully random), so it is encrypted or packed.
→ Static disassembly of the file on disk cannot work. The code has to be read from memory after D2RLoader has mapped it.

### 2.3 Existing plugins (reference only, not modified)
All 4 plugins (`d2rl-charm-inv`, `d2rl-ethereal-ground-tag`, `d2rl-ruffneckk-mass-identify`, `monsterdisplay`):
- export exactly `D2RLoaderGetPluginInfo`, `D2RLoaderLoadPlugin`, `D2RLoaderUnloadPlugin`
- import **nothing** from D2RCore/D2R. The whole API is passed in as a pointer at load time.
- `d2rl-ethereal-ground-tag` imports `msvcrt.dll`, so it was built with **MinGW**. Your plugin can be built the same way.

Install locations: global `<game>\d2rloader\plugins\` + `config\<id>.toml`, or mod-scoped `<game>\Mods\D2RMM\d2rloader\plugins\` (where D2RMM currently installs Monster Display).

### 2.4 Official Plugin SDK – https://github.com/D2RLoader/PluginSDK (commit `03a5af9`, SDK 0.4.0, ABI 4)
What it provides that we need:
- `LifecycleService` → `GameplayEvent` `LocalPlayerReady`, `LevelChanged` (level ids), `difficulty` (0/1/2). Runs on the UI thread.
- `ThreadService::runOnGameThread`: runs a callback on the authoritative game thread (single player / host only).
- `InstallInlineHook(rva, expectedBytes, ...)` and `PatchBytes`. Needs `PluginFlags::NativeHooks`.
- Config TOML helpers (`EnsureConfig` / `ReadConfig`), logging, console commands.

What it does **not** provide: no portal or object creation, no quest-flag access, and no `pGame` / player-unit pointer inside game-thread callbacks.

### 2.5 Game logic reference – D2MOO (D2 1.10 reimplementation), `A1Q4.cpp`
`ACT1Q4_CreateCowPortal(pGame, pUnit)` is what the cube recipe calls (cube output type `CUBEOP_COWPORTAL = 1`, through a function table in `PLRTRADE_CreateCubeOutputs`). It already does every check itself:
1. Portal already opened in this game (global flag `A1Q4 / QFLAG_CUSTOM7`) → fail
2. Cow King killed in this difficulty (`A1Q4 / QFLAG_CUSTOM6`) → fail
3. Expansion: Baal quest `A5Q6` reward not granted (Classic: `A4Q2`) → fail
4. Player not in Rogue Encampment (level 1) → fail
5. Otherwise it creates a permanent portal to level `LEVEL_MOOMOOFARM` next to the player and sets the "opened this game" flag.

On every failure it calls `SUNIT_AttachSound(pUnit, 20, pUnit)` (the "I can't" voice line).

`QUESTS_ChangeLevel(pGame, oldLevel, newLevel, pUnit)` runs server-side on every level transition. It is a natural hook point because it hands us `pGame` and the player unit.

### 2.6 Toolchain check (done here in the cloud)
MinGW-w64 GCC 13 compiles the SDK's `hello-console` example. The DLL exports the 3 required symbols and imports only `KERNEL32` + `msvcrt`, the same profile as `d2rl-ethereal-ground-tag`. **Not yet tested in-game.**

## 3. Interpretation (hypotheses, not yet confirmed)
- H1: D2R 3.3.0 still contains an equivalent of `ACT1Q4_CreateCowPortal` reachable from the cube output table. *Basis: same game logic; D2R kept the cube recipe.* **Unverified for 3.3.0.**
- H2: Calling it from a level-change hook when `newLevel == 1` gives exactly the wanted behaviour (once per game, only for unlocked difficulties, respects the Cow King rule). *Unverified.*
- H3: The failure sound can be suppressed by hooking the sound call while our own call runs. Otherwise every town visit in a locked difficulty would play "I can't".
- H4: Entering a new game directly in Act 1 also goes through a level-change path (rather than only a room-enter path). *Unverified; it may need a second hook.*
- Limitation: works only where you are the authoritative game (single player / TCP host). A TCP/IP client cannot open it.

## 4. Unknowns
- Whether D2RLoader 1.3.0-beta accepts plugin ABI 4 or only 3. The probe will log `context->abiVersion`.
- Whether D2RLoader allows a plugin to read the whole mapped image (expected yes, since it is plain process memory).
- Exact RVAs / struct offsets in build 93847. That is Step 2.
- Whether D2R 3.3.0 changed the Cow King restriction.

## 5. Next step (proposed – Step 1, needs your OK)
Build `cowportal-probe.dll` (diagnostic only, no game changes):
- on load, logs exeBase, buildVersion, ABI version, and the PE section table of the mapped image
- writes the mapped image to `<game>\d2rloader\logs\cowportal-probe\d2r_image_93847.bin` (≈65 MB, local only, never committed to git)
- logs every `LocalPlayerReady` / `LevelChanged` / `QuestCompleted` event with difficulty, to confirm event semantics
- touches no game memory and installs no hooks

## Sources
- D2RLoader Plugin SDK: https://github.com/D2RLoader/PluginSDK
- D2RLoader mod page: https://www.nexusmods.com/diablo2resurrected/mods/1119
- D2MOO: https://github.com/ThePhrozenKeep/D2MOO (`source/D2Game/src/QUESTS/ACT1/A1Q4.cpp`, `PLAYER/PlrTrade.cpp`, `QUESTS/Quests.cpp`)
