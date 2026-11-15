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

Сделано 15.11.2026, сборка и `ctest` прошли. Поля пульта и экрана пишутся в файл сразу (как прежний цвет плиток), точечной заменой фрагмента (`zoneEditing::patchValue`, `appendScreen`, `removeScreen`): остальной текст файла, его отступы и порядок ключей не меняются. При несохранённых зонах сначала вопрос «сохранить?».
- [x] Новая вкладка «Пульт»: название, подключение (`driver::kAll`), тип конфигурации (`configFormat::kAll`) с подсказкой, что программа умеет с выбранной парой (`writableCombination`, `readableConnection`), сенсорный экран, цвет и рамка плиток (`tiles.color`, `tiles.border`). Если на каких-то экранах плитки — картинки, это написано под полями.
- [x] Цвета разделены. Вкладка «Экраны» для открытого экрана: цвет фона (`background.color`), картинка фона (`background.image`), картинка плитки (`cell.image`), цвет панелей (`panel.color`), цвет подписей панелей (`panel.text`), зазор плиток (`grid.gap`, отменяемая правка вместе с зонами), `centerLastRow`, `compact`. Под полями — что перекрыто картинками (экран `compose`, картинка фона, картинка плитки).
- [x] `panel.color` добавлен: на экранах без `compose` подложки `ScreenPanel` рисуются заливкой по `role` (`band` — полоса, `oval` — кольцо, `side` — скруглённая панель), по умолчанию темнее фона, под шапкой и плитками. Раньше они рисовались как плитки. У 1000/1100 панели на спрайтах, поле там помечено как перекрытое.
- [x] Рамка плиток задаётся (`tiles.border`).
- [x] Поле «Дополнительно (JSON)» убрано; `role` у `ScreenPanel` — выпадающий список.
- [x] `touch` у экранов убран: сенсорность — свойство пульта, `capabilities.touch` (по замечанию владельца). `RemoteView` берёт её оттуда; поле экранов из файлов прошлой версии переносится `migrateRemote()`; штатные remote.json и `tools/convertRemotes.py` поправлены.
- Остальные поля: `display.tint` — только для 600-серии и в отрисовке не используется (остался от `make600.py`), в редактор не добавлен; `compose`/`layers` привязаны к спрайтам и правятся вне редактора.
- Попутно закрыт пункт этапа 4 о двойном разборе: редактор читает файл один раз через `remoteResources::readRemote()` (текст с диска, перевод старой схемы, описание), черновик экрана берёт образец из уже прочитанного описания. `RemoteView::overrideLayout` заменён на `overrideScreen` (экран целиком).
- Тесты: `ZoneEditing.PatchValue*`, `AppendAndRemoveScreen`, `PatchGridWritesNewGapOnlyWhenNotDefault`; `RemoteResourcesTest.ScreenTouchMovesToRemoteCapabilities`, `ReadRemoteForEditingUsesDiskTextAndCurrentSchema`; `ZoneEditorDialog.RemoteFieldsAreWrittenToFileRightAway`, `PanelRoleIsChosenFromList`.

Дополнение 15.11.2026 (по замечаниям владельца, сборка и `ctest` прошли):
- [x] Зона плиток `grid.area` `[x, y, w, h]`: на экране в редакторе — рамка «Зона плиток», двигается и растягивается мышью или полями X, Y, W, H; плитки сразу раскладываются в ней сеткой «колонки × ряды» с зазором. Без поля в файле зона считается по плиткам и не пишется, пока её не изменить. `normalizeScreen` ставит ячейки по `col`/`row` внутри зоны.
- [x] Служебные элементы экрана флажками «Показывать на экране»: заголовок, номер страницы, часы, батарея — ячейки `ScreenLabel` (`Title`, `Page`, `Clock`, `Battery`), место правится на холсте. Отрисовка единая (`RemoteView::serviceText`, `paintBattery`): на экранах из картинок, компактных и на экранах с картинкой фона; на «стеклянном» экране 900 они в его шапке, об этом подсказка.
- [x] Общая зона плиток пульта `tiles.area`; экран со своей зоной (`grid.area`, флажок «Своя зона плиток у этого экрана») её перекрывает. Перемещение общей зоны перекладывает плитки на всех экранах без своей, одним шагом отмены. Без `tiles.area` в файле общая — самая частая зона экранов (у 1000/1100 — экранов активностей), остальные считаются своими.
- [x] Флажок `compact` убран (по решению владельца): белая шапка и вкладка Devices 600–700 нарисованы на картинке фона `sprites/screen_600/650.png`, надписи экрана (заголовок, страница, часы, батарея, кнопки у экрана) рисуются поверх фона цветом «Цвет надписей» (`panel.text`, у 600–700 `#111111`). Подписи плиток по центру — по ширине плитки, а не по флажку. Старые файлы переводит `migrateRemote()`.
- [x] Зазор плиток и цвета видны сразу при вводе, в окне выбора цвета — под курсором.
- Тесты: `RemoteResources.GridCellsAreLaidOutInsideTileArea`, `ZoneEditing.PatchGridKeepsTileArea`, `ZoneEditorDialog.TilesFollowTileAreaAndServiceItemsToggle`.

## Этап 4. Интерфейс

Сделано 15.11.2026, сборка и `ctest` прошли у владельца:
- [x] Авто-зоны без блокировки окна: `QProcess` запускается асинхронно (`startAutoZones`), интерпретаторы пробуются по очереди (`python`, `py -3` или `HARMONY_AUTO_ZONES_PYTHON`). На время распознавания кнопка называется «Остановить авто-зоны», повторное нажатие отменяет (`stopAutoZones`). Распознавание останавливается и при закрытии окна, и при смене модели. Ограничения в 20 с больше нет. Результат всегда ложится в корпус, даже если в это время открыт экран.
- [x] Редактор зон читает `remote.json` через `remoteResources::readRemote()`, файл разбирается один раз (этап 3a).
- [x] Кэш `shifts` в `drawCellIcon` (`remoteView.cpp`) сбрасывается по `resources::revision()`, как остальные кэши картинок.
- [x] Вопрос «сохранить?» теперь задаётся в `openFolder()` после чтения файла, а не до выбора. Это касается меню «Недавние», окна «Открыть», стартового окна, файла из хаба и встроенного примера (`openDemo`). Если выбор отменён или файл не открылся, вопроса нет. Если пользователь сохранил правки, файл перечитывается, потому что он мог быть тем же. «Создать» уже спрашивал после выбора. «Закрыть», выход, перезапуск при смене языка и «Открыть отдельно» спрашивают сразу: выбора файла там нет.
- [x] Цвета перенесены в `theme`:
  - `theme::deviceColor(index)` заменил `RemotePanel::deviceColour`. Вызовы поправлены в `remotePanel`, `configView`, `activityDialog`, `buttonDialog`.
  - Подсветка кнопок на фото: `RemoteDimFill`, `RemoteDimLine`, `RemoteBrokenFill`, `RemoteSelectedGlow`, `RemoteHoverLine`. Рамки «без команды» и «выбрана» берутся из тех же `RemoteBroken` и `RemoteSelected`, что образцы в легенде. Корпус без фото — `RemoteBody`.
  - Цвета самого экрана пульта (градиенты 900, плитки, шапка) остались в `remoteView.cpp`: это оформление экрана, оно задаётся в `remote.json`. Значение по умолчанию вынесено в `DEFAULT_TILE_COLOUR`.
- [x] Модификаторы в подсказках редактора зон подставляются из системы. Добавлена `labels::modifier()`: «Ctrl», «Shift», «Alt», на macOS — «⌘», «⇧», «⌥». Для «Выделить всё, затем Удалить» используется `labels::shortcut()`. Русский перевод обновлён.
- Тесты:
  - `ZoneEditorDialog.AutoZonesFlowVisibleOnCanvas` ждёт асинхронного завершения (до 60 с);
  - новые: `ZoneEditorDialog.AutoZonesCanBeStopped` (без Python пропускается), `Theme.DeviceColoursRepeatAndNoDeviceIsGrey`, `Theme.RemoteOverlayColoursAreTranslucent`, `Labels.ModifierNamesHaveNoTrailingPlus`.

## Этап 5. Связь с пультом и CLI

Сделано 15.11.2026, сборка и `ctest` прошли у владельца:
- [x] `Harmony900Link::readConfig`: размер 0 — ошибка «пульт сообщил пустую конфигурацию». `ReadRegionDone` отправляет страж (`DoneGuard`) на любом выходе после `ReadRegion`: при ошибке размера, ошибке или пустой порции данных, отмене. Отправляется ровно один раз; при успехе ошибка `ReadRegionDone` по-прежнему возвращается.
- [x] `tcpChannel.cpp`: ожидание сокета вынесено в `waitSocket()` с общим сроком. Прерванный сигналом `poll` ждёт остаток времени — и в `connectTcp`, и в `receive`. Прерванный сигналом неблокирующий `connect` (`EINTR`) ждётся так же, как `EINPROGRESS`.
- [x] `remoteServices.cpp`: `readHttpResponse()` вместо `readAll()`. Тело читается по `Content-Length` (без ожидания закрытия соединения); без него — до закрытия или паузы, как раньше. Пределы: заголовки 16 КБ, ответ 1 МБ; больше или тело короче заявленного — ошибка.
- [x] CLI `backup` и резервная копия перед `write`: запись через `<имя>.tmp` и `rename` (`cli/src/cliFiles.cpp`, `writeFileAtomic`). Если файл уже есть, копия получает номер: `backup-2.EZHex`, `backup-3.EZHex`…; утилита пишет, под каким именем сохранено.
- Тесты:
  - `Harmony900Link.ZeroConfigSizeIsAnError`, `ReadErrorMidwayLeavesReadMode`, `CancelledReadLeavesReadModeOnce`; в `ReadsConfigAsConcordanceBackup` — `ReadRegionDone` ровно один;
  - `RemoteServices.BodyIsReadByContentLength`, `IncompleteBodyIsAnError`, `TooLargeResponseIsAnError`;
  - `CliFiles.FreePathKeepsExistingFiles`, `WriteLeavesNoTemporaryFile`, `FailedWriteKeepsNothing` (новый `tests/cliFilesTest.cpp`).
- Повтор `EINTR` проверить тестом нельзя (нужен настоящий сигнал во время `poll`), только чтением кода.

## Этап 6. Сборка и CI

Локальной сборкой эти пункты не проверить: результат покажет прогон CI (macOS, Windows, Dependabot).

- [x] Закрепить Actions по SHA, в первую очередь `softprops/action-gh-release` (v3.0.3); заодно `jurplel/install-qt-action` (v4.5.0). Включён Dependabot (`.github/dependabot.yml`, еженедельно, одним PR).
  - [ ] Осталось: `msys2/setup-msys2` и `actions/*` — полный SHA найти не удалось (нет доступа к GitHub), стоят теги. Узнать: `git ls-remote <repo> refs/tags/<тег>`.
- [x] sha256 hidapi вписан (`cmake/Dependencies.cmake`); hidapi теперь качается архивом, git — только с `-DHARMONY_HIDAPI_FROM_GIT=ON`. Хеш взят из Buildroot/Flathub/Ubuntu, хеш подтверждён локальной сборкой win-release (hidapi скачан архивом, проверка прошла).
- [x] «ignoring duplicate libraries» на macOS: прямое подключение `harmony::core` убрано из `app`, `cli`, `harmony_tests`, `harmony_gui_tests` (приходит через `harmony::transport`, PUBLIC). Проверка — лог линковки macOS.
- [x] libconcord под clang: `-Wno-inconsistent-missing-override` для `harmony_concord`.
- Правило: тесты, где структуры инициализируются перечислением полей (`DeviceDraft`, `Packet`), при добавлении нового поля снова дадут `-Wmissing-field-initializers`. Такие места переводить на присваивание полей, когда будут правиться.
- [x] Windows в CI: ccache (`mingw-w64-x86_64-ccache`) и кэш `.ccache` + `_deps`, после сборки — `ccache --show-stats`. В ключ кэша всех задач добавлен `third_party/libconcord/CMakeLists.txt`.

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
