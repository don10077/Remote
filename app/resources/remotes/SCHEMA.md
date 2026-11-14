# remote.json: единая схема пульта (schemaVersion 1)

Один файл на модель вместо profile.json + zones.json + screens.json + общего layouts.json.
Старые файлы удалены; загрузчик достраивает их во внутреннем снимке из remote.json.

```
{
  "schemaVersion": 1,
  "id": "900", "name": "Harmony 900",
  "capabilities": { "touch": true },
  "connection":   { "transport": "usb-net|usb-hid|hub|none", "driver": "...", "configFormat": "..." },
  "hardware":     { "image": "photo.png", "size": [w, h], "zones": [ {id, rect, radius, kind} ] },
  "display":      { "rect": [x, y, w, h], "tint": "#rrggbb" },
  "screens": [ Screen ]
}
```

## Screen

| Поле | Смысл |
|---|---|
| `id` | devices, activities, command, favorites, transport, keypad |
| `role` | activity, favorites, devices, command, media, numpad |
| `ref` | система координат экрана `[w, h]` |
| `grid` | `{cols, rows, gap?}`. Если задан, экран рисует единый рендерер сеток. Ячейка без `rect` ставится по `col`, `row` (с нуля) и `colSpan`, `rowSpan` (по умолчанию 1); `gap` — отступ в единицах `ref` (по умолчанию 4) |
| `background` | `{color, image}`: есть картинка, рисуем её, иначе заливка цветом. По умолчанию цвет 900 |
| `cell` | `{color, border, image, shadow, text}`: общий стиль ячеек. По умолчанию заливка как фон и белая рамка |
| `cells` | ячейки: `{id, rect, radius, kind, button?, span?}` |
| `layers` | необязательные картинки по слоям (бывший screens.json) |

Правило отрисовки: картинка задана и загрузилась, используем её; иначе заливаем цветом.
Спрайты 900 лежат в `sprites/*_900.png`, 600-серии в `sprites/*_600.png`, 650 в `background_650.png`.
