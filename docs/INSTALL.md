# Installation

Requirements: Diablo II: Resurrected **3.3.0 build 93847**, plus [D2RLoader](https://www.nexusmods.com/diablo2resurrected/mods/1119) with plugin ABI 4 (tested with 1.3.0-beta).

Download from [Releases](https://github.com/CloneAtWork/D2R-Auto-Cow-Portal/releases). Use **one** of the two methods below and keep only one copy of the DLL.

## Method A – D2RMM

1. Close the game.
2. Copy `cow-portal-in-town-<version>-d2rmm.zip` **without unpacking it** into the `d2rloader` folder of your D2RMM installation, for example:
   ```
   D:\D2RMM Custom 1.9.9\d2rloader\cow-portal-in-town-1.0.0-d2rmm.zip
   ```
3. Start D2RMM. It imports the zip on start, or when you choose **Plugins > Refresh**. A successful import creates `…\d2rloader\cow-portal-in-town-<version>-d2rmm\manifest.json` with two entries and `"warnings": []`:
   - `plugins\cow-portal-in-town.dll` (role `plugin`)
   - `config\cow-portal-in-town.toml` (role `config`)
4. Install your mods in D2RMM as usual. D2RMM copies the files to:
   ```
   <game>\Mods\D2RMM\d2rloader\plugins\cow-portal-in-town.dll
   <game>\Mods\D2RMM\d2rloader\config\cow-portal-in-town.toml
   ```
5. If you installed the plugin manually before, delete `<game>\d2rloader\plugins\cow-portal-in-town.dll`.

## Method B – manual

1. Close the game.
2. Unpack `cow-portal-in-town-<version>.zip`.
3. Copy `manual\plugins\cow-portal-in-town.dll` to `<game>\d2rloader\plugins\`.
4. Optional: copy `manual\config\cow-portal-in-town.toml` to `<game>\d2rloader\config\`. If you skip this, the plugin creates the file with the defaults on first start.

`<game>` is your D2R installation folder, the one that contains `D2RLoader.exe`.

## Check that it is loaded

Start the game through D2RLoader as usual.

1. `<game>\d2rloader\logs\d2rloader.log`, in the startup plugin list:
   ```
   [PLUGIN]     Cow Portal in Town 1.0.0 by Alex [global]      (manual install)
   [PLUGIN]     Cow Portal in Town 1.0.0 by Alex [mod]         (D2RMM install)
   ```
2. The plugin log, at `<game>\d2rloader\logs\cow-portal-in-town.log` (manual) or `<game>\Mods\D2RMM\d2rloader\logs\cow-portal-in-town.log` (D2RMM):
   ```
   cow-portal-in-town 1.0.0: enabled=1 normal=1 nightmare=1 hell=1 anchor=bonfire offset=(6,9) fallback=1 diagnostics=0 build=93847
   Hooks installed (UpdateProgress 0x406550, ChangeLevel 0x5155C0, SpawnPlayer 0x485350); client signal on. Ready.
   ```
3. Load a character that has beaten Baal (or Diablo in Classic) in the current difficulty and go to the Rogue Encampment. The red portal appears next to the campfire.

## Troubleshooting

Set `[diagnostics] enabled = true` in the config, restart the game, and reproduce the problem. Then look at the plugin log:

| Log line | Meaning |
|---|---|
| `Unsupported game build '…'` | The game is not build 93847. The plugin is inactive (safe); it needs an update for your build. |
| `Expected bytes mismatch at …` | The game code differs from what the plugin expects (game patch, or another plugin hooked the same function). The plugin is inactive (safe). |
| `difficulty not unlocked (quest 40 not completed), no portal.` | Baal is not done in this difficulty (quest 26 = Diablo in Classic). This is expected. |
| `… disabled in config, skipped.` | That difficulty is set to `false` in the config. |
| `cow portal already opened in this game.` | There is already a portal in this game (it opens only once per game). |
| `portal scheduled; waiting until the client is in town.` | Arrival detected. |
| `[client] level A -> B.` | The game client reported a level change (1 = Rogue Encampment). |
| `client reports in town after N frames (X ms).` | The client finished loading; the portal follows ~0.5 s later. |
| `no client signal after 250 frames …; creating anyway.` | No client event within ~10 s (for example a remote TCP/IP player); the portal is created anyway. |
| `Cow portal opened in the Rogue Encampment (Hell) at (x,y); bonfire (…), requested (…).` | Success. |
| `bonfire not found after … frames; opening next to the player instead.` | The campfire object never appeared (should not happen in an unmodified town); fallback used. |

If something does not work, open an [issue](https://github.com/CloneAtWork/D2R-Auto-Cow-Portal/issues) and attach `cow-portal-in-town.log` (with diagnostics on) and `d2rloader.log`.

## Uninstall

- **D2RMM:** delete the imported plugin from D2RMM's `d2rloader` folder (the zip and the `cow-portal-in-town-<version>-d2rmm` folder), then install your mods again.
- **Manual:** delete `<game>\d2rloader\plugins\cow-portal-in-town.dll`, and optionally `<game>\d2rloader\config\cow-portal-in-town.toml`.

Your characters stay valid: the plugin never writes save data. D2RLoader lists it in each character's plugin history because it is a gameplay plugin.

## Notes

- If you edit the config of a D2RMM installation, D2RMM *may* restore the packaged default the next time it installs mods. This is unverified. If your changes disappear, edit the config inside D2RMM's imported plugin folder instead, or re-apply them after installing.
- The plugin only does something where your PC runs the game server: single player, or when you host a TCP/IP game.
