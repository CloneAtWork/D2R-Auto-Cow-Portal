# Building and testing

## Toolchain

The plugin is plain C++20 and is built with **MinGW-w64 GCC 13+**. It is linked fully statically, so the DLL imports only `KERNEL32.dll` and `msvcrt.dll`, the same profile as other MinGW-built D2RLoader plugins.

| Platform | Setup |
|---|---|
| Ubuntu/Debian | `sudo apt install g++-mingw-w64-x86-64-posix python3` |
| Windows | [MSYS2](https://www.msys2.org/), then in the UCRT64 shell: `pacman -S mingw-w64-ucrt-x86_64-gcc python`<br>Set `CXX=g++ WINDRES=windres`, because the MSYS2 tools have no prefix |

The D2RLoader Plugin SDK is a git submodule pinned to commit `03a5af9` (SDK 0.4.0, plugin ABI 4):

```bash
git clone --recurse-submodules https://github.com/CloneAtWork/D2R-Auto-Cow-Portal.git
# or, in an existing clone:
git submodule update --init
```

## Build

```bash
./build.sh            # build/cow-portal-in-town.dll
./build.sh probe      # also build/cowportal-probe.dll (diagnostic, see UPDATING.md)
./build.sh package    # also dist/cow-portal-in-town-<v>.zip, dist/cow-portal-in-town-<v>-d2rmm.zip, dist/SHA256SUMS.txt
```

`src/cow_portal_in_town.rc` embeds two resources:
- **1001:** the plugin ABI manifest (DWORD 4), which D2RLoader requires
- **1002:** the default config `src/cow_portal_in_town.toml`, which D2RMM's importer uses

When you change the default config, keep `src/cow_portal_in_town.toml` and `kDefaultToml` in `src/cow_portal_in_town.cpp` identical.

Builds are **reproducible**. `build.sh` links with a fixed image base (`0x180000000`), without a link timestamp, and strips at link time; the archives use fixed timestamps and CRLF text. Rebuilding a tagged commit with the same toolchain (Ubuntu 24.04 `g++-mingw-w64-x86-64-posix` 13.2) gives the hashes published in `SHA256SUMS.txt`.

## Tests

`tests/test_harness.cpp` is a Windows program that **simulates D2RLoader and the game server**:
- Every native game function the plugin calls is replaced by a mock, through a jump written at the real RVA inside a fake image. A mock being hit proves the address arithmetic.
- `CheckExpectedBytes` and `InstallInlineHook` are answered from a **real runtime dump of D2R build 93847**, so every expected-byte constant in the plugin is checked against the actual game code.
- A fake game object (object hash list at `+0x2A30`, units linked through `+0x158`) exercises the plugin's own campfire search.
- Server frames are calls of the hooked `GAME_UpdateProgress`; client events are `LifecycleService` callbacks fired by the test.

It runs 61 checks: arrival paths (game start, in-act waypoint, cross-act waypoint without a hook, client signal before or after the server), timeouts, fallbacks, locked difficulties, Classic vs LoD, config parsing (CRLF, BOM, comments, invalid values), and refusal on a wrong build or changed bytes.

```bash
tests/run_tests.sh /path/to/d2r_image_93847.bin     # Linux: needs wine64; Windows: MSYS2 shell
```

The dump is **not** included. It is Blizzard's code and must not be redistributed. Create your own with the probe; see [UPDATING.md](UPDATING.md#1-dump-the-running-game). The CI workflow therefore only builds and checks exports and imports; the behavioural tests run locally.

## Code map

| File | Content |
|---|---|
| `src/cow_portal_in_town.cpp` | The plugin. RVAs, expected bytes and struct offsets at the top; then config parser, game helpers, scheduling, hooks, install |
| `src/cow_portal_in_town.rc` | Manifest and default-config resources |
| `src/cow_portal_in_town.toml` | Default config (shipped in the release) |
| `tests/test_harness.cpp` | Host and game simulator |
| `tools/probe/` | `cowportal-probe`: read-only plugin that dumps the mapped game image and logs lifecycle events |
| `tools/re/d2img.py` | Python helpers (capstone, numpy) for the dump: `.pdata` function table, RIP-relative and rel32 xrefs, disassembly |
| `tools/package.py` | Release zip builder |
