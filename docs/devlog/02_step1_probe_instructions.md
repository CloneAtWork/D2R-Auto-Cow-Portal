# Step 1 – Run the cowportal-probe (manual steps)

The probe is read-only. It installs no hooks, patches no bytes, and does not change your characters (role = Client, so it is not recorded in character plugin history).

## Do this

1. Close D2R and D2RMM.
2. Copy `D:\Diablo II Resurrected\_CowPortalDev\step1-probe\cowportal-probe.dll`
   into `D:\Diablo II Resurrected\d2rloader\plugins\` (this folder is currently empty).
   - Global folder on purpose: D2RMM rebuilds `Mods\D2RMM\…` on every install and could remove it from there.
3. Start the game the way you normally do (D2RLoader with mod D2RMM).
   - Expect startup to take ~1–3 s longer **once**, while the ~65 MB dump is written.
4. Load an **offline character that has completed at least Normal** (ideally one that has Nightmare/Hell unlocked as well).
5. In the game, do the following and remember the order:
   1. Enter the game in any act.
   2. Use a waypoint to go to the **Rogue Encampment**, if you are not already there.
   3. Take one waypoint to any outdoor area, then back to the Rogue Encampment.
   4. Save & Exit.
6. Close the game.
7. Copy the plugin's 3 output files to `D:\Diablo II Resurrected\_CowPortalDev\step1-results\`, or simply tell me they exist and I will pick them up:
   - `D:\Diablo II Resurrected\d2rloader\logs\cowportal-probe\d2r_image_93847.bin` (~65 MB)
   - `D:\Diablo II Resurrected\d2rloader\logs\cowportal-probe\d2r_image_93847.txt`
   - the probe's log file in `D:\Diablo II Resurrected\d2rloader\logs\` (named `cowportal-probe*.log`, or its lines inside `d2rloader.log`)
8. Tell me which difficulty you loaded, and whether that character has already killed the Cow King there.

## Do NOT

- Do not edit or move `D2R.exe`, `D2RCore.dll` or anything in `Mods\`.
- Do not share the `.bin` file publicly. It is a copy of Blizzard's game code, for local analysis only, and is never committed to git.
- Do not leave the probe installed after this step. Delete `d2rloader\plugins\cowportal-probe.dll` once the files are collected.

## If something goes wrong

- **Plugin not loaded:** send me `d2rloader\logs\d2rloader.log`. Look for lines mentioning `cowportal-probe` or `ABI` (I can rebuild for ABI 3).
- **Game crashes on start:** delete the DLL and send me the newest file in `d2rloader\crashes\`.
