# Harmony remote resources

The working folder is `resources` next to the executable. The application creates it
and restores missing bundled files on startup and reload. Existing files, including
modified files, are never overwritten automatically. Resources are loaded from this folder.

Changes are applied after saving, or via Remote → Reload remote resources.
An error in a bundled remote keeps the previous valid set; a remote you added that has
an error simply does not appear in the list, and the zone editor shows the reason.
Delete a bundled file and reload to restore its original version. Keep backups of
custom models: they cannot be restored from the bundled files.

## Layout

```
sprites/                  screen images (backgrounds, tiles, icons)
remotes/<id>/
  remote.json             remote description: photo, buttons, screens
  photo.png               remote photo with transparent background
```

## remote.json

```json
{
  "schemaVersion": 1,
  "id": "my-remote",
  "name": "My remote",
  "capabilities": { "touch": false },
  "connection": { "driver": "usb-hid" },
  "hardware": {
    "image": "photo.png",
    "size": [450, 1500],
    "zones": [
      { "id": "VolumeUp", "rect": [50, 714, 70, 74], "radius": 14, "kind": "Key" }
    ]
  },
  "display": { "rect": [112, 216, 228, 284] },
  "screens": []
}
```

| Field | Meaning |
|---|---|
| `id` | same as the folder name: lowercase Latin letters, digits, `_`, `-` |
| `name` | name in the model list |
| `hardware.size` | `[width, height]` of the photo in pixels, must match the PNG |
| `hardware.zones` | buttons on the photo |
| `display.rect` | `[x, y, width, height]` of the screen on the photo |
| `connection.driver` | how the program talks to the remote: `usb-net` — USB network (900, 1000, 1100, 1100i); `usb-hid` — USB HID (One, 600–700); `mh` — MyHarmony over the network; `hub` — via Harmony Hub; `none` — no connection. Empty or missing — the catalog value; a value set here wins over the catalog, and for a model that is not in the catalog it is the only way to choose the connection. An unknown name rejects the model |
| `screens` | the remote's screens with tiles and cells; easiest to create in the zone editor |

The model catalog is the reference and decides whether writing to the remote is allowed:
remote.json chooses only how the program talks to the remote, not whether it may write. The
format of the configuration comes from the catalog family and from the file that was read.

Zone: `rect` is x, y, width, height in photo pixels from the top-left corner.
`radius` is the corner radius; a negative value is a fraction of the shorter side
(`-0.5` is a circle). `kind`: `Key` (a button that gets commands, `id` is the
configuration button name), `System`, `ScreenActs`, `ScreenSoft`, `PagePrev`, `PageNext`.
A zone must lie inside the photo; ids must be unique.

## New model

Copy a similar model's folder to `remotes/<new-id>/`, change `id` and `name` in
`remote.json`, replace the photo (and `hardware.size`) and adjust the zones and screens
in the zone editor. The model appears in the remote choice. A similar look does not
mean the same connection protocol.
