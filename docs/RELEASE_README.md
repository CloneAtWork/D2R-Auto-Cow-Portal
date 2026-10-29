# Cow Portal in Town (D2RLoader plugin) – v{VERSION}

Project page, source code and issues: https://github.com/CloneAtWork/D2R-Auto-Cow-Portal

Opens the **secret cow level portal** in the **Rogue Encampment** automatically, once per game, for every difficulty your character has unlocked. You no longer need to fetch Wirt's Leg or cube it with a Tome of Town Portal.

- **Unlocked** means that difficulty's final quest is done: *Eve of Destruction* (Baal) in Lord of Destruction / Reign of the Warlock, or *Terror's End* (Diablo) in Classic. This is the same rule the game uses for the cube recipe.
- The portal appears **about 0.3–0.5 s after you arrive** in the Rogue Encampment:
  - when a game starts there,
  - by waypoint from another act,
  - by walking or waypoint within Act 1.
- It is placed at a **fixed spot next to the central campfire**. The offset is configurable, and the game picks the nearest free, walkable spot.
- In a difficulty you have **not** unlocked, nothing happens: no portal and no "I can't" sound.
- Killing the Cow King does **not** block it. D2R 3.3.0 no longer has that rule.

## Requirements

| | |
|---|---|
| Game | Diablo II: Resurrected **3.3.0, build 93847** (only this build, see *Limitations*) |
| Loader | D2RLoader with plugin ABI 4 (tested with **1.3.0-beta**) |
| Mode | Offline single player, or as **host** of a TCP/IP game |

## Package contents

```
cow-portal-in-town-{VERSION}.zip
├─ README.md                            this file
├─ cow-portal-in-town-{VERSION}-d2rmm.zip   for installation method A (D2RMM)
└─ manual\
   ├─ plugins\cow-portal-in-town.dll    for installation method B (manual)
   └─ config\cow-portal-in-town.toml    default configuration
```

## Installation

Use **one** of the two methods, not both.

### A) With D2RMM (recommended if you use D2RMM)
1. Close D2R.
2. Copy `cow-portal-in-town-{VERSION}-d2rmm.zip` (do not unpack it) into the `d2rloader` folder of D2RMM, e.g. `D:\D2RMM Custom 1.9.9\d2rloader\`.
3. Start D2RMM. It imports the zip on start, or when you run **Plugins > Refresh**.
4. Install your mods in D2RMM as usual. The plugin is installed to `<game>\Mods\D2RMM\d2rloader\plugins\` and its config to `<game>\Mods\D2RMM\d2rloader\config\`.
5. If you installed an older copy manually, delete `<game>\d2rloader\plugins\cow-portal-in-town.dll`, so that only one copy exists.

### B) Manually
1. Close D2R.
2. Copy `manual\plugins\cow-portal-in-town.dll` to `<game>\d2rloader\plugins\`.
3. Optional: copy `manual\config\cow-portal-in-town.toml` to `<game>\d2rloader\config\`. If you skip this, the plugin creates the file with default values on first start.

### Check that it is loaded
In `<game>\d2rloader\logs\d2rloader.log` you should see:
```
[PLUGIN]     Cow Portal in Town {VERSION} by Alex [global]     (or [mod] when installed via D2RMM)
```
and in the plugin log:
- manual install: `<game>\d2rloader\logs\cow-portal-in-town.log`
- D2RMM install: `<game>\Mods\D2RMM\d2rloader\logs\cow-portal-in-town.log` (same place as other D2RMM-installed plugins, e.g. `monsterdisplay.log`)

It should contain:
```
Hooks installed (UpdateProgress 0x406550, ChangeLevel 0x5155C0, SpawnPlayer 0x485350); client signal on. Ready.
```

## Configuration – `cow-portal-in-town.toml`

Location:
- manual install: `<game>\d2rloader\config\`
- D2RMM install: `<game>\Mods\D2RMM\d2rloader\config\`

Note (not verified): D2RMM may overwrite this file with the packaged default whenever it reinstalls mods. If your changes disappear, edit the imported copy inside D2RMM's `d2rloader` folder, for other plugins this is `<D2RMM>\d2rloader\<plugin>\source\config\`, or re-apply your changes after installing.

Changes take effect after restarting the game.

```toml
[cow_portal]
enabled = true        # false = the plugin installs nothing at all
normal = true         # per-difficulty switches
nightmare = true
hell = true

[placement]
anchor = "bonfire"    # "bonfire" = fixed spot near the campfire, "player" = next to you (like the cube)
offset_x = 6          # offset from the campfire in sub-tiles (1 tile = 5 sub-tiles)
offset_y = 9
fallback_to_player = true   # if the campfire cannot be found, open it next to you instead

[diagnostics]
enabled = false       # true = log every arrival and decision (for troubleshooting)
```

**Moving the portal.** The view is isometric, so on screen:
- `+offset_x` moves it **down-right**, `-offset_x` **up-left**
- `+offset_y` moves it **down-left**, `-offset_y` **up-right**

Change the values in steps of 2–3. Example: `offset_x = 6`, `offset_y = 12` puts it a bit further to the lower left than the default.

## Limitations
- **Only game build 93847.** The plugin calls the game's own functions at fixed addresses. After a D2R patch it **refuses to load**, which is safe: it changes nothing, and the log says `Unsupported game build` or `Expected bytes mismatch`. It then needs an update for the new build.
- **Host only.** As a client in someone else's TCP/IP game you get the portal only if the host runs the plugin. For other players in a game you host, the portal is opened at the latest ~10 s after they arrive.
- **Plugin history.** This is a gameplay (*Server*) plugin, so D2RLoader records it in the character's plugin history. Your save files are **not** changed. The portal only exists in the running game, exactly like a cube-made portal.
- The position was tuned and confirmed in the test player's games. The camp layout varies between 4 versions; if the spot is ever blocked, the game picks the nearest free spot.

## Troubleshooting
1. Set `[diagnostics] enabled = true` in the config and restart the game.
2. Reproduce the problem.
3. Look at the plugin log (see *Check that it is loaded* for its location). Typical lines:
   - `difficulty not unlocked (quest 40 not completed), no portal.`: Baal is not done in this difficulty.
   - `portal scheduled …`, then `client reports in town …`, then `Cow portal opened … at (x,y)`: everything worked.
   - `Unsupported game build` / `Expected bytes mismatch`: the game was patched; the plugin needs an update.

## Uninstall
- **D2RMM:** remove the plugin in D2RMM (or delete the zip from D2RMM's `d2rloader` folder), then reinstall your mods.
- **Manual:** delete `<game>\d2rloader\plugins\cow-portal-in-town.dll`, and optionally `<game>\d2rloader\config\cow-portal-in-town.toml`.

## How it works (short)
| What | Game function (build 93847) |
|---|---|
| Notice arrivals in the Rogue Encampment | `QUESTS_ChangeLevel` 0x5155C0 and `CLIENTS_SpawnPlayer` 0x485350, plus a check every 5 server frames in `GAME_UpdateProgress` 0x406550 (for cross-act waypoints) |
| Wait until your client has loaded the area | D2RLoader lifecycle event *LevelChanged → Rogue Encampment*, plus ~0.5 s |
| Check unlock | the game's quest flags (A5Q6 / A4Q2, reward granted) |
| Place and open | campfire position + offset → the game's free-position search → `CreatePortalObject` with the same arguments as the cube recipe → the per-game flag "cow portal opened" |

Before installing anything, all of these functions are verified byte by byte.
