# Harmony remote resources

The working folder is `resources` next to the executable. The application creates it
and restores missing bundled files on startup and reload. Existing files, including
modified files, are never overwritten automatically. Resources are loaded from this
folder; no folder selection is needed.

Changes are applied after saving, or via Remote → Reload remote resources.
Invalid changes keep the previous valid snapshot. Delete a bundled file and reload
to restore its original version. Keep backups of custom models: they cannot be
restored from the bundled files.

## Layout

```
layouts.json              screen layouts (shared by all remotes)
sprites/                  images for 1000/1100 screens (screens.json)
remotes/<id>/
  profile.json            model description
  photo.png               remote photo with transparent background
  zones.json              buttons on the photo
  screens.json            screen images (optional)
```

## profile.json

| Field | Meaning |
|---|---|
| `id` | same as the folder name: lowercase Latin letters, digits, `_`, `-` |
| `name` | name in the model list |
| `image` | always `"photo.png"` |
| `size` | `[width, height]` of the photo in pixels, must match the PNG |
| `screen` | `[x, y, width, height]` of the screen on the photo, or `null` |
| `modes` | screen layout per mode: `{"activities": "list_3", "command": "grid_2x3"}`, ids from `layouts.json` |
| `driver` | `none` — no connection; `hid` — read a backup over USB HID; `harmony900` — Harmony 900 network protocol |
| `configFormat` | `unsupported` or `harmony900` |

Writing to the remote is available only for `driver: harmony900` together with `configFormat: harmony900`.

## zones.json

An array of zones. Coordinates are photo pixels from the top-left corner:

```json
{ "id": "VolumeUp", "rect": [50, 714, 70, 74], "radius": 14, "kind": "Key" }
```

`rect` is x, y, width, height. `radius` is the corner radius; a negative value is a
fraction of the shorter side (`-0.5` is a circle). `kind`: `Key` (a button that gets
commands, `id` is the configuration button name), `System`, `ScreenActs`, `ScreenSoft`,
`PagePrev`, `PageNext`. A zone must lie inside the photo; ids must be unique.

## New model

Copy a similar model's folder to `remotes/<new-id>/`, change `id` and `name` in
`profile.json`, replace the photo (and `size`) and adjust the zones. The model appears
in the remote choice. A similar look does not mean the same connection protocol.
