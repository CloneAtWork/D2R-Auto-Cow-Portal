# Configuration

File: `cow-portal-in-town.toml`
- manual install: `<game>\d2rloader\config\`
- D2RMM install: `<game>\Mods\D2RMM\d2rloader\config\`

The plugin creates the file with the defaults below if it is missing. Changes take effect after **restarting the game**. Unknown keys are ignored; invalid values keep their default, and the log says so (`Config: [section] key has an invalid value; default kept.`).

```toml
[cow_portal]
enabled = true
normal = true
nightmare = true
hell = true

[placement]
anchor = "bonfire"
offset_x = 6
offset_y = 9
fallback_to_player = true

[diagnostics]
enabled = false
```

## `[cow_portal]`

| Key | Default | Description |
|---|---|---|
| `enabled` | `true` | Master switch. `false` installs no hooks at all, so the plugin is fully inert. |
| `normal` / `nightmare` / `hell` | `true` | Open the portal in this difficulty, if it is unlocked. |

## `[placement]`

| Key | Default | Description |
|---|---|---|
| `anchor` | `"bonfire"` | `"bonfire"`: fixed spot relative to the central Rogue campfire.<br>`"player"`: next to the player, exactly like the cube recipe. |
| `offset_x` | `6` | Offset from the campfire in sub-tiles (1 tile = 5 sub-tiles). Range −1000…1000. |
| `offset_y` | `9` | Same, second axis. |
| `fallback_to_player` | `true` | If the campfire cannot be found within ~15 s, or no free spot exists near the target, open the portal next to the player instead. |

### Which way does the offset move the portal?

The game uses isometric coordinates. On screen:

```
   -x (up-left)         -y (up-right)
             \           /
               campfire
             /           \
   +y (down-left)       +x (down-right)
```

- `offset_x = 6, offset_y = 6`: straight below the campfire
- `offset_x = 6, offset_y = 9`: below and slightly to the left (**default**)
- `offset_x = 9, offset_y = 6`: below and slightly to the right
- `offset_x = 0, offset_y = 10`: to the lower left

Change the values in steps of 2–3. The game always moves the portal to the nearest free, walkable spot around the requested point, so a portal never ends up inside a tent or a wall. The Rogue Encampment has four layout variants. The default offset was tuned and confirmed in-game, but not systematically in all four variants. If the spot is ever blocked, the free-spot search still keeps the portal walkable.

## `[diagnostics]`

| Key | Default | Description |
|---|---|---|
| `enabled` | `false` | `true` logs every arrival, client level change and decision to the plugin log. Startup lines, warnings, errors and the one "portal opened" line per game are always logged. |

## Examples

Hell only, cube-like placement next to you:
```toml
[cow_portal]
normal = false
nightmare = false
hell = true

[placement]
anchor = "player"
```

Temporarily disable the plugin without uninstalling it:
```toml
[cow_portal]
enabled = false
```
