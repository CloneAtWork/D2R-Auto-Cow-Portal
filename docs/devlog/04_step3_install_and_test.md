# Step 3 – Install and test cow-portal-in-town 0.1.0 in-game

Supported: D2R 3.3.0 **build 93847** with D2RLoader (tested so far: 1.3.0-beta loads ABI 4 plugins).
On any other build the plugin refuses to load and logs why. It never hooks unknown code.

## Install

1. Close D2R and D2RMM.
2. Make sure the probe is gone: `D:\Diablo II Resurrected\d2rloader\plugins\cowportal-probe.dll` should not exist.
3. Copy `D:\Diablo II Resurrected\_CowPortalDev\step3-plugin\cow-portal-in-town.dll`
   into `D:\Diablo II Resurrected\d2rloader\plugins\`.
4. Optional: copy `cow-portal-in-town.toml` from the same folder into `D:\Diablo II Resurrected\d2rloader\config\`.
   If you skip this, the plugin creates its own config on first start. Diagnostics logging is ON for this test.

## Test (please do all four and note what you see)

| # | Do | Expected |
|---|---|---|
| A | Load your Hell character in **Hell** so it **starts in Act 1** (Rogue Encampment). | A red portal near you on arrival, or not (that is what this test answers). |
| B | Take a waypoint out of town, then back to the Rogue Encampment. | A red portal appears next to the waypoint (if it did not already open in A). |
| C | Step into the portal. | You arrive at the Moo Moo Farm. |
| D | Save & Exit, then start a new **Normal** game with the same character. Then, if you have a character who has *not* beaten Baal in some difficulty, start that difficulty and go to the Rogue Encampment. | Normal: portal appears. Locked difficulty: no portal and **no "I can't" voice line**. |

If the character's last act was not Act 1, use a waypoint to go to the Rogue Encampment.

## Send back

1. `D:\Diablo II Resurrected\d2rloader\logs\cow-portal-in-town.log`, or just tell me it exists and I will collect it.
2. The results of A–D: did the portal appear, where, and was any sound played.
3. `D:\Diablo II Resurrected\d2rloader\logs\d2rloader.log`, only if the game crashed or the plugin did not show up as loaded.

## Do NOT
- Do not edit `D2R.exe`, `D2RCore.dll` or anything in `Mods\`.
- Do not use it online (Battle.net). D2RLoader is offline / TCP-IP only anyway.

## Uninstall
Delete `d2rloader\plugins\cow-portal-in-town.dll` (and optionally `d2rloader\config\cow-portal-in-town.toml`).
Characters stay valid: the plugin changes no save data, only opens the portal in the running game.
D2RLoader does record Server plugins in the character's plugin history.

## Known limitations
- Works only where you are the game host (single player or TCP/IP host).
- Build 93847 only. After a game patch the RVAs must be found again; the probe from Step 1 plus `re/d2img.py` make that repeatable.
- The portal opens next to where you stand when you arrive. That is the native behaviour of the cube recipe.
