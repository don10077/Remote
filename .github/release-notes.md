# Harmony 0.7.0

**English** · [Русский](#русский)

Harmony is a free, open-source desktop editor for Logitech Harmony remotes on Windows,
Linux and macOS, with local configuration backups and no dependency on Logitech's cloud.

## What's new

- Zone editor: drag screen header and footer boundaries, set their colours and individual
  button and tile colours. The tile area stays between the header and footer.
- Simplified configuration capture dialog for USB remotes and network reads from Harmony Hub.
  USB captures use `*.remote.json`, automatic model detection and validation after saving.
  Writing was removed from this dialog; it remains available through the editor and CLI
  for supported models and formats.
- Shared binary container validation checks section tables, markers, addresses and checksums.
  GSPM handling and capture validation use these checks. `harmony-cli info` reports container
  details for supported container families, including `*.remote.json` captures.
- Windows builds and packages now use MSYS2 UCRT64.
- Updated English and Russian documentation.

## Downloads

| System | Package |
|---|---|
| Windows 10/11 x64 | `Harmony-0.7.0-setup.exe` or `Harmony-0.7.0-windows-portable.zip` |
| Linux x86_64, glibc 2.39+ | `Harmony-0.7.0-x86_64.AppImage` |
| macOS, Apple Silicon and Intel | `Harmony-0.7.0-macos.dmg` |

`SHA256SUMS.txt` contains package checksums. The packages include `harmony-cli`.
Windows builds are unsigned and macOS builds use an ad-hoc signature; the operating
system may ask you to approve the application on first launch.

## Remote support

Reading, editing and writing have been tested on **Harmony 900 under Windows**.
Other models are experimental; container validation does not establish hardware support.
Run `harmony-cli models` for model-specific transfer capabilities. Communication with
remotes on Linux and macOS still needs hardware testing.

To help extend support, read your remote's configuration and send the `*.remote.json`
file to [don10077@mail.ru](mailto:don10077@mail.ru), with the remote model in the subject.
Harmony 665 capture is awaiting a hardware test by its owner.

This is a **pre-release**. Keep a backup before writing. Harmony is an unofficial project,
distributed under GPL-3.0-only without warranty. Logitech and Harmony are Logitech trademarks.

[Project and instructions](https://github.com/don10077/Remote) ·
[Website](https://don10077.github.io/Remote/) ·
[Report an issue](https://github.com/don10077/Remote/issues) ·
[Support development](https://www.donationalerts.com/r/alexshkurko)

---

## Русский

**Предварительная версия (pre-release).**

### Новое в 0.7.0

- Редактор зон: высоту шапки и подвала экрана можно менять перетаскиванием границ,
  задавать цвета этих областей и отдельных кнопок и плиток. Область плиток остаётся
  между шапкой и подвалом.
- Упрощён диалог чтения конфигураций всех моделей: чтение по USB и из Harmony Hub
  по сети. USB-копии сохраняются в `*.remote.json` с автоматическим определением модели
  и проверкой после сохранения. Из этого диалога убрана запись; запись через редактор
  и CLI остаётся доступной для поддерживаемых моделей и форматов.
- Общая проверка бинарных контейнеров: таблицы секций, маркеры, адреса и контрольные
  суммы. Она используется при обработке GSPM и проверке копий конфигураций.
  `harmony-cli info` показывает сведения о поддерживаемых семействах контейнеров,
  в том числе при анализе файлов `*.remote.json`.
- Сборка и пакеты Windows переведены на MSYS2 UCRT64.
- Обновлена документация на русском и английском, добавлен двуязычный сайт проекта,
  улучшено окно «О программе».

### Файлы

| Система | Файл |
|---|---|
| Windows 10/11 x64 | `Harmony-0.7.0-setup.exe` или `Harmony-0.7.0-windows-portable.zip` |
| Linux x86_64, glibc 2.39+ | `Harmony-0.7.0-x86_64.AppImage` |
| macOS, Apple Silicon и Intel | `Harmony-0.7.0-macos.dmg` |

`SHA256SUMS.txt` содержит контрольные суммы. В пакеты включён `harmony-cli`.
Windows-сборка не подписана, macOS использует локальную подпись ad-hoc;
при первом запуске система может запросить разрешение.

### Поддержка пультов

Чтение, редактирование и запись проверены на настоящем **Harmony 900 под Windows**.
Остальные модели экспериментальные: проверка контейнера сама по себе не подтверждает
работу с пультом. Возможности переноса по моделям доступны командой `harmony-cli models`.
Связь с пультами на Linux и macOS ещё требует проверки на оборудовании.

Чтобы помочь расширить поддержку, считайте конфигурацию своего пульта и пришлите
`*.remote.json` на [don10077@mail.ru](mailto:don10077@mail.ru), указав модель в теме письма.
Чтение Harmony 665 ожидает проверки владельцем пульта.

Держите резервную копию перед записью. Программа неофициальная, не связана с Logitech
и распространяется без гарантий на условиях GPL-3.0-only. Запись меняет память пульта.

[Проект и инструкции](https://github.com/don10077/Remote/blob/master/README.ru.md) ·
[Сайт](https://don10077.github.io/Remote/ru/) ·
[Сообщить об ошибке](https://github.com/don10077/Remote/issues) ·
[Поддержать проект](https://www.donationalerts.com/r/alexshkurko)
