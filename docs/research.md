# Другие проекты: что они умеют / Other projects

Справка по соседним проектам, на которые опирается Harmony. Возможности указаны для
**самого проекта**, а не для Harmony; что есть в Harmony — [models.md](models.md).

## Сравнение / Comparison

Речь о возможностях **самого указанного проекта**, а не о том, что уже перенесено в Harmony.
Concordance передаёт готовую конфигурацию; редактор устройств и активностей в нём отсутствует.

| Проект / Project | Модели / Models | Чтение / Read | Правка или генерация / Edit or build | Запись / Write | Основание / Evidence |
|---|---|---|---|---|---|
| Concordance / libconcord | 745; 748/768; старые 6xx; 720/785/88x/AV-100/TC30; 36x/51x/52x/55x; 890/895; One; 700; 900; 1000/1000i/1100/Xbox; 200/300; Link | Есть пути чтения, ограничения зависят от модели | Нет — передача готового файла | Обновление конфигурации отмечено Working в SupportedModels.md | Таблица upstream и реализация транспорта; это не тест нашей программы |
| Concordance / libconcord | 800 prototype | Эксп.; MH-чтение проверено на одном устройстве | Нет | В таблице не подтверждена | Отдельная строка architecture 18 |
| Afterglow | 900 | Да | Да, генерация PK/XML | Да | Профиль `verified` |
| Afterglow | 1100, skins 62/63/64 | Да | Да; Device Sequence, встроенные action lists, экранные страницы | Да, экспериментально | Профиль `experimental`; в notes есть сообщения о записях тестировщиков |
| Afterglow | One, arch 12 / skin 54 | Да | Да, генератор GSPM из проекта; импорт с ограничениями распознавания | Да, экспериментально | Профиль `experimental`; notes сообщают о загрузке образов и работе активностей |
| Afterglow | 800 prototype, arch 18 / skin 86 | Ограниченно: импорт собственных сгенерированных конфигураций | Да, GSGD; клавиатура ещё не привязана полностью | Эксп.; нужен libconcord с поддержкой 800 | Профиль и backend `harmony_800`; общий подтверждённый статус записи не заявлен |
| harmony900tools | 900 | Да, из Qt-интерфейса через libconcord; резервная копия и восстановление | Да: редакторы устройств, активностей, кнопок и ИК-команд | Да, сборка ZIP типа H900 и updateUserConfigData | src/ui/concordConnection.cpp, редакторы src/ui/devices и activities; аппаратная проверка здесь не выполнялась |
| re-harmony | Только One, arch 12 / skin 54, FW 0.5.0 | Да, модифицированная libconcord | Да: устройства, команды, назначения физических кнопок, генерация поверх baseline | Да, область конфигурации | README: проверено на одном пульте; macOS, Windows не проверена |

«Старые 6xx» в таблице Concordance (architecture 7) **не следует путать** с
600/650/665 архитектуры 14. Таблица Concordance объединяет 1000/1100 с architecture 15,
но `remote_info.h` отдельно описывает 1100 как architecture 11; для идентификации
использована более конкретная информация из кода и профилей Afterglow.
Concordance не даёт в SupportedModels.md отдельной матрицы чтения: колонка
Config Update доказывает заявленную запись готового файла, а не успешное чтение каждой модели.

## Источники / Sources

Снимки локальных репозиториев: Concordance `dca4505`, Afterglow `83d67ff`,
harmony900tools `ca59616`, re-harmony `992a58c`. Анализировалось рабочее дерево,
поэтому незакоммиченные изменения также могли входить в проверенный код.

- [Concordance: модели](../../concordance/SupportedModels.md), [карты памяти и skins](../../concordance/libconcord/remote_info.h), [classic HID](../../concordance/libconcord/remote.cpp), [Z/USB-network](../../concordance/libconcord/remote_z.cpp).
- [Afterglow: README](../../afterglow/README.md), [профили](../../afterglow/src/afterglow/library/remotes), [проверки записи](../../afterglow/src/afterglow/remotes.py), [чтение/запись](../../afterglow/src/afterglow/concord.py), [формат 1100](../../afterglow/docs/harmony_pk/ssir-sequences.md).
- [harmony900tools: обзор](../../harmony900tools/doc/general.md), [чтение и запись из GUI](../../harmony900tools/src/ui/concordConnection.cpp), [обёртка libconcord](../../harmony900tools/src/wrappers/concordWrapper.cpp).
- [re-harmony: возможности и ограничения](../../re-harmony/README.md), [проверки GSPM](../../re-harmony/config_work/configcheck.py).
- Harmony: [профили и изображения](../app/resources/remotes), [HID reader](../transport/src/harmonyHidReader.cpp), [сетевой транспорт](../transport/src/harmony900Link.cpp), [перенос готовых файлов](../transport/src/concordTransfer.cpp), [HID-копии](hid-models.md).
