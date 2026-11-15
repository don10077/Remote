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

## tiles

Необязательный блок верхнего уровня `"tiles": {"color": "#rrggbb", "border": "#rrggbb"}`: оформление плиток
одно на весь пульт. Поля из `tiles` перекрывают `cell` каждого экрана, поэтому плитки на всех экранах
одинаковые. Редактор экранов пишет цвет именно сюда и убирает `cell.color` и `cell.border` у отдельных экранов.
Блок `tiles` есть у всех пультов. У 1000 и 1100 плитки заданы спрайтами (`cell.image`), поэтому цвет
в `tiles` там хранится, но не виден: спрайт главнее.

## Screen

| Поле | Смысл |
|---|---|
| `id` | devices, activities, command, favorites, transport, keypad |
| `role` | activity, favorites, devices, command, media, numpad |
| `ref` | система координат экрана `[w, h]` |
| `grid` | `{cols, rows, gap?}`: сколько плиток рисовать (колонки × ряды). Есть у каждого экрана с плитками, редактор обновляет его при сохранении зон. Ячейка без `rect` ставится по `col`, `row` (с нуля) и `colSpan`, `rowSpan` (по умолчанию 1); `gap` — отступ в единицах `ref` (по умолчанию 4) |
| `background` | `{color, image}`: есть картинка, рисуем её, иначе заливка цветом. По умолчанию цвет 900 |
| `cell` | `{color, border, image, shadow, text}`: общий стиль ячеек. По умолчанию заливка как фон и белая рамка |
| `cells` | ячейки: `{id, rect, radius, kind, button?, span?}` |
| `layers` | необязательные картинки по слоям (бывший screens.json) |

Общих именованных раскладок нет: экран целиком описан в `remote.json` своего пульта, поля `layout` нет.
Внутреннее имя экрана — `<модель>_<экран>` (например `900_devices`).

Единая схема отрисовки для всех пультов: фон (спрайт экрана целиком `compose`, картинка `background.image`
или стеклянный фон 900), затем плитки по `grid` (спрайт `cell.image` или плитка 900 цветом из `tiles`),
поверх плиток зоны `cells`. `style: h600` (600–700) добавляет белую шапку, вкладку Devices и спрайт фона
`background_600/650`.

Правило отрисовки: картинка задана и загрузилась, используем её; иначе заливаем цветом.
Спрайты 900 лежат в `sprites/*_900.png`, 600-серии в `sprites/*_600.png`, 650 в `background_650.png`.
