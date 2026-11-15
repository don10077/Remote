# План работ

Составлен 15.11.2026 по обзору всего проекта. Заменяет `docs/review-2026-11-02.md`:
его пункты почти все закрыты, оставшиеся перенесены сюда.
Отметка `[x]` — сделано и собрано у владельца.

Цель проекта — поддержать в программе разные пульты, а не только 900. Нынешние профили —
начало; решения принимаются так, чтобы новый пульт добавлялся данными и адаптером формата,
а не правками по всему коду.

Правила:
- после каждого этапа владелец собирает проект и запускает `ctest`, и только потом начинается следующий этап;
- коммиты делает владелец;
- `dist/win/resources/remotes/880` лежит только во внешней папке и в `app/resources` не переносится. Совместимость с ним при изменениях схемы не требуется.

## Этап 1. Документация (сделано 15.11.2026)

Цель: в каждой теме один актуальный документ, без устаревших описаний.

**1.1. Ресурсы пультов.** Сейчас три файла описывают старую схему: `profile.json`, `zones.json`, `layouts.json`, `screens.json`, стиль `h600`. Удалены они в пользу `remote.json`.
- [x] `app/resources/remotes/README.md` удалить. Его содержание переходит в `SCHEMA.md`.
- [x] `app/resources/README.md` переписать под `remote.json`. Что оставить:
  - фото и размеры холста;
  - значения `kind`, `key`, `goto`, `page`, `system`;
  - что проверено, а что снято на глаз;
  - правки без пересборки;
  - как добавить модель.

  Описания `layouts.json`, `profile.json` и `modes` убрать.
- [x] `app/resources/export/README.{ru,en}.md` переписать. Эти файлы получает пользователь при выгрузке ресурсов, и сейчас они учат создавать `profile.json` и `zones.json`. Без правки проверку на 880 по инструкции не пройти.
- [x] `SCHEMA.md`: дописать поля ячеек (`kind`, `key`, `goto`, `page`, `system`, `centerLastRow`, `layers`, `compose`). Сейчас они описаны только в старом README. Строку про `style: h600` поправить после этапа 3.
- [x] `sprites/readme.md` перенести в раздел «Спрайты» `SCHEMA.md` и удалить.

**1.2. Корень и `docs/`.**
- [x] `ideas.txt` удалить: этап 1–2 устарел по `core-refactoring-plan.md`, единая схема экранов сделана. Файл и так не в git.
- [x] `docs/review-2026-11-02.md` удалить: заменён этим планом.
- [x] `remote.md` переименовать в `docs/models.md`. Таблицу моделей и ограничения оставить. Блок «Сравнение проектов» и «Источники» (ссылки на `../concordance` и другие соседние папки) перенести в `docs/research.md`. Удалить внутреннюю заметку «после указания пользователя не собирать…». Обновить ссылки в README и `.github/release-notes.md`.
- [x] `docs/harmony-one-support.md` и `docs/hid-reading.md` свести в один `docs/hid-models.md`: формат `*.hid.json`, статус One и 600-серии, что нужно для редактора GSPM. Записи «добавлено 07.11» и «09.11» превратить в текущее состояние.
- [x] `docs/hub-reading.md`: исправить устаревшее «MapList.json не разбирается» и «импорт в редактор не реализован». Сейчас есть `fromHub`, разбор MapList и подбор кодов из базы.
- [x] `docs/core-refactoring-plan.md`: отметки «ждёт сборки» заменить на фактический статус и дописать риски из обзора (этап 2 ниже).
- [x] `docs/read-665.md`: сверить пример версии (`v0.9.0`) с текущей.

**1.3. README.md (653 строки).**
- [x] В README оставить: описание, скачать и установить, подключение, сборку, архитектуру, ссылки.
- [x] Разделы-справку по функциям перенести в `docs/user-guide.md`: «Проверка и запись», «Обучение команд», «Макросы» … «Замена устройства».
- [x] Дорожную карту заменить ссылкой на этот план. Пункт «Дальше: профили 1000, 1100» уже сделан.

**1.4. Мусор вне git (только место на диске).**
- [x] `build/`: удалить `win-release-before-move-*`, `cc*.txt`, `v*.txt`, `runcc*.bat`, `hello.*`, логи.
- [x] Удалить `CMakeFiles/` в корне: это след запуска cmake в исходниках.
- [x] `logs_*.zip` удалить: лог CI уже неактуален.

## Этап 2. Ядро: документ хаба

Сделано 15.11.2026, сборка и `ctest` прошли у владельца:
- [x] Формат документа хранится в XML: свойство `<Property name="SourceFormat">Hub</Property>`. Отдельного поля `format` в `Impl` больше нет, формат читается из модели. Поэтому он переживает сохранение в папку и в `.EZHex`, повторное открытие, отмену.
- [x] Сохранение хаба в `.EZHex` оставлено разрешённым (это основной формат файла программы), но теперь такой файл открывается снова как хаб и на пульт не пишется. Вместо запрета из первоначального плана.
- [x] `check()` для хаба: команды без ИК-кода не проверяются по таблицам; вместо ошибки на каждую команду — одно предупреждение на устройство «У N из M команд ещё нет ИК-кода». Bluetooth, сеть и Home Assistant в счёт не идут. Для 900 проверка не изменилась.
- [x] `commandWithoutData`: новый ИК-код убирает `Transport` и `CodeSource`, `Native` остаётся. `copyCommands` переносит `CodeSource` вместе с кодом.
- [x] `ConfigBackend` принадлежит документу: `backend()` возвращает `const ConfigBackend*`, тот же объект при каждом вызове, действителен и после перемещения документа.
- [x] Тесты в `tests/configHubDocumentTest.cpp`: `FormatSurvivesSavingToFolder`, `FormatSurvivesSavingToEzHex`, `BackendIsOwnedByDocumentAndSurvivesMove`, `CheckDoesNotBlameMissingTablesForCommandsWithoutCode`, `LearningCodeTurnsCommandIntoPlainIr`.

Не сделано:
- [ ] Хранить исходный JSON хаба в документе, чтобы не терять поля, которые модель не знает (`ContentProfileKey`, `ProtocolId`, долгие нажатия, остальные раскладки MapList). Это фактически начало `HubBackend` (этап 3 `core-refactoring-plan.md`); имеет смысл, когда появится запись на рутованный хаб.

## Этап 3. Правило «экраны только из remote.json»

- [x] Поле `style` убрано (15.11.2026, сборка и `ctest` прошли):
  - 600–700: `"compact": true` и `background.image` в `remote.json` (у 600 — `background_600.png`, у 650/665/700 — `background_650.png`, как было). Ветки `h600` в `remoteView.cpp` удалены, новый экран наследует `compact` от образца.
  - Палитры `t1000`/`t1100`/`h900` (`ScreenStyle`, ~110 строк) удалены: из них использовался только цвет подписей панелей. Он теперь в `panel.text` (у 1000 — `#2a2d30`, у остальных белый по умолчанию).
- [x] Пульт по умолчанию — первый в каталоге: `harmony::defaultRemoteProfile()` / `defaultRemoteId()` в `remoteProfile.h` (порядок задаёт `cmake/RemoteProfiles.cmake`). Литерал «900» убран из `remoteView.cpp`, `configView.cpp`, `mainWindow.h`; заголовок карточки в `modernShell.cpp` — название этого пульта (15.11.2026, сборка и `ctest` прошли).
- [x] Прямое чтение `remote.json` (15.11.2026, сборка и `ctest` прошли):
  - `remoteResources` больше не достраивает в снимке `profile.json`, `zones.json`, `screens.json`, `layouts.json` (`expandRemoteJson` и `synthetic` удалены). Новое API: `remote(id)` — проверенный remote.json пульта; `normalizeScreen/normalizeRemote` — `rect` ячеек по `grid`.
  - Из JSON читается только `remotes/<id>/remote.json`; старые файлы в папке ресурсов игнорируются (тест `LeftoverFilesOfOldSchemaAreIgnored`). Проверки `reload()` переписаны на поля remote.json (`hardware.*`, `display.rect`, `screens[].cells/layers`).
  - `RemoteView`: `modes` хранит сами экраны, `tiles` — общий блок плиток; `layouts`, `profileDirectory`, `readJson`, флаги `grid`/`glass`/`pill` удалены. Картинки собранных экранов — из `layers`. `overrideLayout` правит экран и `tiles.color`.
  - `hardKeyIds`: кнопки экранов в прежнем порядке (экраны по алфавиту id, ячейки по файлу).
  - Тесты `remoteViewTest`, `remoteResourcesTest` читают `remoteResources::remote()`; `tools/autoZones.py` читает `display.rect` и `hardware.zones`.
- Проверка 880 снята: на него больше не ориентируемся (см. начало плана). Его remote.json по-прежнему проходит новые проверки.

- [x] Переименование `connection` в `remote.json` (15.11.2026, сборка и `ctest` прошли): два поля вместо трёх. `remote.json` в старой схеме (connection, style) переводится на новую в памяти (`remoteResources::migrateRemote`) с сохранением правок пользователя; редактор зон записывает его уже в новой схеме.
  - `driver`: `usb-net` (900, 1000, 1100, 1100i), `usb-hid` (One, 600–700), `hub`, `none`. Поле `transport` удалено.
  - `format`: `harmony-xml` (900), `device-sequence` (1000, 1100, 1100i), `gspm` (One), `arch14` (600–700), `hub-json`, `none`. Вместо `configFormat`.
  - Старые значения не принимаются. Списки — `driver::kAll` и `configFormat::kAll` в `core/include/harmony/core/remoteProfile.h`; запись — `writableCombination()`. `usesHarmony900()` → `usesUsbNet()`.
  - Поправлены `remoteResources.cpp`, `cmake/RemoteProfiles.cmake`, `mainWindow*.cpp`, `remoteResourcesTest.cpp`, `tools/convertRemotes.py`, `SCHEMA.md`, `export/README.*`.

## Этап 3a. Редактор пульта (редактор зон)

Цель: всё, что есть в `remote.json`, можно задать в редакторе, без ручной правки файла.
- [ ] Выбор способа подключения (`usb-net`, `usb-hid`, `hub`, `none`) — список в свойствах пульта.
- [ ] Выбор типа конфигурации (формат из этапа 3) — список в свойствах пульта. Подсказка: запись на пульт возможна только для реализованной пары «подключение + формат».
- [ ] Поле цвета в редакторе сейчас подписано как цвет экрана, а меняет только `tiles.color`. У 1000/1100/1100i плитки и панели — спрайты, поэтому цвет там не виден вовсе. Разделить на понятные поля: цвет фона экрана (`background.color`), цвет плиток (`tiles.color`), цвет подписей панелей (`panel.text`); если у экрана задана картинка, показывать, что цвет перекрыт ею.
- [ ] Цвет самих панелей (`ScreenPanel`: полосы, боковые панели, овал) сейчас нигде не задаётся: у 1000/1100 они нарисованы на спрайтах, у остальных пультов их нет. Решить, нужен ли `panel.color` для экранов без спрайтов.
- [ ] Цвет рамки плиток (`tiles.border`). Сейчас редактор задаёт только цвет плиток, а рамка всегда белая (`zoneEditorDialog.cpp:1824`).
- [ ] Поле «Дополнительно (JSON)» убрать: `key`, `goto`, `page`, `system`, `button` уже задаются списками, без редактора остаётся только `role` у `ScreenPanel` — добавить для него выпадающий список (`band`, `oval`, `side`).
- [ ] Проверить, каких ещё полей `remote.json` нет в редакторе (`display.tint`, `capabilities.touch`, `background.image`, `cell.image`, `grid.gap`, `centerLastRow`), и добавить нужные.

## Этап 4. Интерфейс

- [ ] Автоматическая разметка зон: асинхронный `QProcess` с отменой вместо `waitForFinished(20000)` (`zoneEditorDialog.cpp:1186`).
- [ ] Редактор зон читает `remote.json` через `remoteResources`, а не сам (в функции у `:1721` файл разбирается дважды).
- [ ] Статический кэш `shifts` (`remoteView.cpp:263`) сбрасывать по `resources::revision()`.
- [ ] Вопрос «сохранить?» задавать после выбора файла: `mainWindow.cpp:311, 337, 425, 774, 1004`.
- [ ] Палитру устройств (`remotePanel.cpp:99-103`) и зашитые цвета `remoteView.cpp` перенести в `theme`.
- [ ] Ctrl в подсказках редактора зон подставлять через `labels::shortcut`.

## Этап 5. Связь с пультом и CLI

- [ ] `harmony900Link.cpp:165`: `size == 0` считать ошибкой; `ReadRegionDone` отправлять на всех путях выхода.
- [ ] `tcpChannel.cpp:185`: повторять ожидание `connectTcp` после прерывания сигналом.
- [ ] `remoteServices.cpp:33`: ограничение размера и чтение по `Content-Length`.
- [ ] CLI `backup`: запись через временный файл и `rename`; суффикс к имени копии при совпадении.

## Этап 6. Сборка и CI

- [ ] Закрепить Actions по SHA, в первую очередь `softprops/action-gh-release`; включить Dependabot.
- [ ] Вписать sha256 hidapi (`cmake/Dependencies.cmake:21`).
- [ ] macOS выдаёт «ignoring duplicate libraries». Убрать прямое подключение `harmony::core`: оно и так приходит через `harmony::transport`. Места: `app/CMakeLists.txt:237`, `cli/CMakeLists.txt:5`, `tests/CMakeLists.txt:56`, `:109`.
- [ ] libconcord под clang: добавить `-Wno-inconsistent-missing-override` для цели `harmony_concord`.
- [ ] Тесты, где структуры инициализируются перечислением полей (`DeviceDraft`, `Packet`), при добавлении нового поля снова дадут `-Wmissing-field-initializers`. Такие места переводить на присваивание полей, когда будут правиться.
- [ ] Windows в CI: ccache и кэш `_deps`. В ключ кэша добавить `third_party/libconcord/CMakeLists.txt`.

## Этап 7. Разделение крупных файлов

Делать по одному файлу за этап, логику не менять.
- [ ] `zoneEditorDialog.cpp` (1911 строк) разделить на `RemoteJsonStore`, `ScreenWizard`, `AutoZonesRunner` и панель свойств.
- [ ] `remoteView.cpp` (1969 строк) разделить на отрисовку экранов, определение зон под курсором и перетаскивание.
- [ ] `mainWindow.cpp` (2364 строки): вынести меню, операции правки, импорт из базы и проверку.
- [ ] `cli/src/main.cpp` разбить на `fileCommands`, `irdbCommands`, `remoteCommands` и общий `cliCommon`.
- [ ] Ядро: один хелпер вместо 13 повторов связки `writableTable` + `intern`. Разбить `replaceDevice`, `Checker` и `fixProblem`.

## Отложено

- Сверка контрольной суммы `GetUpdateChecksum`: ждём, когда по записям станет ясен алгоритм.
- `RemoteCapabilities`, `HubBackend`, кодек GSPM: см. `core-refactoring-plan.md`.

## Вопросы

- Конфигурации 600–700, One и 1000 мы не умеем ни читать в редактор, ни править (даже без записи на пульт). Это потому, что не написан разбор их форматов, или потому, что для разбора не хватает данных (реальных дампов, описания формата)? Что именно нужно, чтобы открыть каждую из них в редакторе только для правки?
