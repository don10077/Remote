# Harmony 0.6.0

**English** · [Русский](#русский)

Harmony is a free, open-source desktop editor for Logitech Harmony remotes on Windows,
Linux and macOS. Edit devices, activities and button assignments, learn infrared commands,
and keep local configuration backups without Logitech's configuration software or cloud service.

## What's new

- One `*.remote.json` file for configuration captures. It contains the original configuration,
  remote model and firmware, timestamp, program version and SHA-256.
- Automatic model detection when reading through **Remote → Configuration files — all models…**
  or `harmony-cli transfer-read auto`.
- Saved captures are checked for data integrity and container consistency.
- The capture format has been tested with a real Harmony 900. Harmony 665 capture is
  awaiting a hardware test by its owner.
- Fixes to adding and replacing devices and checking configurations.
- Fixed Russian text encoding in the Windows installer.

## Remote support

Configuration reading and writing have been tested on **Harmony 900 under Windows**.
Other models are experimental and have different levels of support. Run `harmony-cli models`
for model-specific transfer capabilities. Linux and macOS packages are built and tested in CI,
while remote communication on those systems still needs hardware testing.

## Help extend remote support

If you can, read your Harmony configuration and email the `*.remote.json` file to
[don10077@mail.ru](mailto:don10077@mail.ru). Put the remote model in the subject,
for example `Harmony 665 configuration`. Captures help us investigate more models.

## Help test Harmony 665 capture

Close Logitech's software, connect the remote by USB and open Harmony.
Choose **Remote → Configuration files — all models…**, select **Harmony 665** and click
**Read configuration to file…**. Save the suggested `*.remote.json` file and send it to the
developer at [don10077@mail.ru](mailto:don10077@mail.ru), with your remote model in the subject. If reading fails, include a screenshot of the error
and any file that was saved. Use the read operation for this procedure.

## Downloads

| System | Package |
|---|---|
| Windows 10/11 x64 | `Harmony-0.6.0-setup.exe` or `Harmony-0.6.0-windows-portable.zip` |
| Linux x86_64, glibc 2.39+ | `Harmony-0.6.0-x86_64.AppImage` |
| macOS, Apple Silicon and Intel | `Harmony-0.6.0-macos.dmg` |

`SHA256SUMS.txt` contains package checksums. The packages include `harmony-cli`.
Windows builds are unsigned and macOS builds use an ad-hoc signature, so the operating
system may ask you to approve the application on first launch.

This is a **pre-release**. Keep a backup before writing to a remote. Harmony is an unofficial
project, distributed under GPL-3.0-only without warranty. Logitech and Harmony are Logitech trademarks.

[Project and instructions](https://github.com/don10077/Remote) ·
[Report an issue](https://github.com/don10077/Remote/issues) ·
[Support development](https://www.donationalerts.com/r/alexshkurko)

---

## Русский

**Предварительная версия (pre-release).**

### Новое в 0.6.0

- Единый файл `*.remote.json` для передачи конфигурации на анализ. В нём сохраняются
  исходный контейнер, модель, прошивка, дата снятия, версия программы и SHA-256.
- Чтение с автоматическим определением модели через «Пульт → Файлы конфигурации — все модели…»
  и `harmony-cli transfer-read auto`. После сохранения проверяются данные и контейнер.
- Новый формат проверен чтением настоящего Harmony 900. Чтение Harmony 665 ожидает проверки
  владельцем пульта.
- Исправления добавления и замены устройств, проверки конфигурации.
- Исправлена кодировка русского языка установщика Windows.

### Помогите расширить поддержку пультов

Если есть возможность, считайте конфигурацию своего Harmony и пришлите `*.remote.json`
на [don10077@mail.ru](mailto:don10077@mail.ru). В теме письма укажите модель пульта,
например «Конфигурация Harmony 665». Эти файлы помогают добавить поддержку других моделей.

### Как прислать конфигурацию Harmony 665

Закройте программу Logitech Harmony, подключите пульт к USB и запустите Harmony.
Откройте «Пульт → Файлы конфигурации — все модели…», выберите Harmony 665 и нажмите
«Прочитать конфигурацию в файл…». Сохраните предложенный `*.remote.json` и пришлите его
на [don10077@mail.ru](mailto:don10077@mail.ru), указав модель в теме письма. При ошибке приложите снимок окна и файл,
если он сохранился. Для снятия копии используйте кнопку чтения.

Редактор конфигурации пультов Logitech Harmony без облака Logitech:
чтение, правка и запись конфигурации на пульт по USB.

### Что умеет

- Чтение и запись конфигурации **Harmony 900** — проверено на настоящем пульте.
- Активности, устройства, кнопки (физические и экранные), макросы, свойства
  устройств и активностей; отмена и повтор правок, версии конфигурации.
- Устройства из базы ИК-кодов Logitech, поиск модели по кнопкам родного пульта,
  обучение ИК-команд с родного пульта, ввод команд вручную.
- Проверка конфигурации и показ изменений перед записью; резервная копия перед записью.
- Файлы `.EZHex` (в том числе от concordance и afterglow).
- Консольная утилита `harmony-cli`.

### Пульты

Запись проверена только на **Harmony 900**. Остальные модели — экспериментальные:
код переноса подключён, но на настоящих пультах из этой программы не проверен.
Список моделей и возможности переноса доступны командой `harmony-cli models`.

### Файлы

| Система | Файл |
|---|---|
| Windows 10/11 x64 | `Harmony-0.6.0-setup.exe` или `Harmony-0.6.0-windows-portable.zip` |
| Linux x86_64 (glibc 2.39+) | `Harmony-0.6.0-x86_64.AppImage` |
| macOS (Apple Silicon и Intel) | `Harmony-0.6.0-macos.dmg` |

`SHA256SUMS.txt` — контрольные суммы файлов.

### Известные ограничения

- Программа не подписана: Windows SmartScreen покажет «Неизвестный издатель»
  («Подробнее» → «Выполнить в любом случае»), macOS — «не удаётся проверить
  разработчика» (Control-щелчок → «Открыть»). Подробнее — в
  [README](https://github.com/don10077/Remote).
- Связь с пультом проверена на Windows. На Linux и macOS пакеты собираются
  и проходят тесты, но с пультом не проверялись.
- Изображения некоторых пультов временные и будут заменены.

### Внимание

Программа неофициальная и не связана с Logitech. Распространяется «как есть»,
без гарантий (GPL-3.0). Запись меняет память пульта — держите резервную копию.

Ошибки и вопросы — [Issues](https://github.com/don10077/Remote/issues).
Поддержать проект — [DonationAlerts](https://www.donationalerts.com/r/alexshkurko).
