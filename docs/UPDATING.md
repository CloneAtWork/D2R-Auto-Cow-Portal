# Updating for a new game build

After a D2R patch the plugin logs `Unsupported game build` or `Expected bytes mismatch` and stays inactive. This is the reproducible procedure used for build 93847.

## 1. Dump the running game

1. `./build.sh probe`, then copy `build/cowportal-probe.dll` to `<game>\d2rloader\plugins\`.
2. Start the game once through D2RLoader and load any character.
3. Close the game. You now have:
   - `<game>\d2rloader\logs\cowportal-probe\d2r_image_<build>.bin`: the mapped image, memory layout (file offset = RVA)
   - `d2r_image_<build>.txt`: section table, region map, hashes
   - `<game>\d2rloader\logs\cowportal-probe.log`: context, sections, and lifecycle events
4. **Remove the probe again.** The dump is Blizzard's code: keep it local, never publish it.

The probe is read-only: no hooks, no patches, `Client` role. It never overwrites an existing dump.

## 2. Re-locate the functions

Update the section bounds at the top of `tools/re/d2img.py` from the `.txt` sidecar, then:

```bash
pip install capstone numpy
D2R_IMAGE=/path/to/d2r_image_<build>.bin python3 -i tools/re/d2img.py
```

Use the same anchors as for build 93847 (details in [TECHNICAL.md](TECHNICAL.md)):

| Target | How to find it |
|---|---|
| Cube output parser and table | `riprel_xrefs(<rva of "Cow Portal">)`, then follow to the dispatch `call [base + type*8 + table]` and read `table[1]` |
| `ACT1Q4_CreateCowPortal` | the jump target of `table[1]`; it gives the offsets for difficulty/version (`game+0x104/0x106`), quest flags (`+0x40`), and the callees GetGlobalState, GetPlayerData, GetQuestState, GetRoom, LevelFromRoom, GetCoords, GetFreePosition, CreatePortalObject, SetGlobalState |
| `QUESTS_ChangeLevel` | a small function in the Quests.cpp code range that stores event `3` and walks `game+0x21D8` callbacks |
| `CLIENTS_SpawnPlayer` | caller of the client room update (also called by ChangeLevel's caller), with three null asserts and `mov dl, 0x15` |
| `GAME_UpdateProgress` | `inc dword [rcx+<frame counter>]` followed by the debug-trigger checks 1/2/4 |
| `SUNIT_IterateUnitsOfType` | loop over 0x80 buckets at `game+0x2230 / 0x2630 / 0x3230` with a callback |
| Layout anchors | `lea r,[rcx+0x2A30]` loop (objects), the get-next accessor `[unit+0x158]`, and the class-id accessor `[unit+4]` |

## 3. Update and verify

1. Update `Rva::…`, the `kExp…` arrays, `kSupportedBuild` and, if changed, the struct offsets in `src/cow_portal_in_town.cpp`.
2. Update the RVAs in `tests/test_harness.cpp` (`WriteAbsJump` targets and `HostHook`).
3. Run `tests/run_tests.sh /path/to/d2r_image_<build>.bin`; all checks must pass.
4. Test in-game: start in town, cross-act waypoint, in-act waypoint, a locked difficulty, and one portal per game. Use `[diagnostics] enabled = true`.
5. Bump the version and update `CHANGELOG.md`.
