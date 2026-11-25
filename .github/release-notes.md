# Harmony 0.8.0

**English** · [Русский](#русский)

Harmony is a free, open-source desktop editor for Logitech Harmony remotes on Windows,
Linux and macOS, with local configuration backups and no dependency on Logitech's cloud.

## What's new

- Automatic update checks, with verified installer downloads for installed Windows copies.
- Clearer USB connection diagnostics and recovery for Harmony USB network adapters on Windows.
- Physical button assignments use model-specific key dictionaries and work without a visual profile.
  The remote panel can be resized or hidden, and model changes show an assignment summary.
- Improved configuration imports, model and family detection, HID reads and experimental
  file-protocol reading for explicitly supported model/file combinations.
- Screen button icons now follow the configuration family; improved profile and screen validation.
- Expanded regression coverage and stricter GCC/Clang builds.

## Downloads

| System | Package |
|---|---|
| Windows 10/11 x64 | `Harmony-0.8.0-setup.exe` or `Harmony-0.8.0-windows-portable.zip` |
| Linux x86_64, glibc 2.39+ | `Harmony-0.8.0-x86_64.AppImage` |
| macOS, Apple Silicon and Intel | `Harmony-0.8.0-macos.dmg` |

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

### Новое в 0.8.0

- Проверка обновлений; установленная Windows-версия загружает установщик с проверкой размера и SHA-256.
- Подробная диагностика USB и восстановление подключения сетевых USB-адаптеров Harmony под Windows.
- Назначения физических кнопок используют словари конкретных моделей и доступны без визуального
  профиля. Панель пульта можно скрывать и менять её размер; при смене модели показана сводка назначений.
- Улучшены импорт конфигураций, определение модели и семейства, чтение HID. Добавлено экспериментальное
  чтение по файловому протоколу для явно поддерживаемых сочетаний модели и файла.
- Значки экранных кнопок учитывают семейство конфигурации; улучшены проверки профилей и экранов.
- Расширены регрессионные тесты и строгие сборки GCC/Clang.

### Файлы

| Система | Файл |
|---|---|
| Windows 10/11 x64 | `Harmony-0.8.0-setup.exe` или `Harmony-0.8.0-windows-portable.zip` |
| Linux x86_64, glibc 2.39+ | `Harmony-0.8.0-x86_64.AppImage` |
| macOS, Apple Silicon и Intel | `Harmony-0.8.0-macos.dmg` |

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
