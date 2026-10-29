# Install and test cow-portal-in-town 0.2.0

## Install
1. Close D2R and D2RMM.
2. Replace `D:\Diablo II Resurrected\d2rloader\plugins\cow-portal-in-town.dll` with
   `D:\Diablo II Resurrected\_CowPortalDev\step4-plugin-v0.2.0\cow-portal-in-town.dll`.
3. **Delete** `D:\Diablo II Resurrected\d2rloader\config\cow-portal-in-town.toml`, so that the new
   default config (with the new `[placement]` section) is created on the next start.
   Alternatively, copy the new `cow-portal-in-town.toml` from the same folder over it.

## Test
| # | Do | Expected |
|---|---|---|
| A | Load the Hell character so it starts **directly in the Rogue Encampment**. | The portal is already there on load, next to the bonfire (slightly south of it on screen). |
| B | Waypoint out and back. | No second portal. |
| C | Start a new game whose last act is not Act 1, then waypoint to the Rogue Encampment. | The portal is at the same place next to the bonfire, not at the waypoint. |
| D | Locked character / difficulty. | No portal, no sound. |

Repeat A in two or three new games: the town layout is one of 4 variants. Note whether the spot looks good in each one.

## Send back
1. Tell me `d2rloader\logs\cow-portal-in-town.log` exists and I'll pick it up.
2. For A–D: did it appear, and how does the position look ("good" / "too close to X" / "blocks Y")?
   A screenshot helps if the position is not right.

## Tuning the position yourself (optional)
In `d2rloader\config\cow-portal-in-town.toml`, section `[placement]`:
- `offset_x`, `offset_y`: offset from the bonfire in sub-tiles. Start with steps of 2–3.
  On screen: `+x` = down-right, `-x` = up-left, `+y` = down-left, `-y` = up-right (isometric).
- `anchor = "player"` restores the cube-like behaviour (portal next to you).
Restart the game after every change.

## Do NOT
- Do not edit `D2R.exe`, `D2RCore.dll` or anything in `Mods\`.
