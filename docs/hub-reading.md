# Harmony Hub: чтение конфигурации и дорога к редактированию

Полный план отвязки ядра от формата 900, частью которого станет редактирование
хаба, — в [docs/core-refactoring-plan.md](core-refactoring-plan.md).

Добавлено 13.11.2026. Экспериментально: на физическом Hub из Harmony не проверялось.

## Зачем

Пульты Elite, Ultimate, Ultimate Home, 950 (с Hub), Pro 2400, Companion и Smart Control
не хранят конфигурацию в формате, который отдаётся по USB: libconcord для architecture 17
не читает `/cfg/usercfg`, а файлы `/data/resources/*.json` доступны только по SSH после
рутинга. Но вся их конфигурация есть на Hub, и стоковая прошивка Hub отдаёт её по
локальной сети без авторизации — так работает интеграция Harmony в Home Assistant.
Рутинг, облако Logitech и учётная запись не нужны.

## Как прочитать и прислать

1. Hub включён и подключён к той же сети, что и компьютер. Узнать его IP-адрес можно
   в роутере (устройство «HarmonyHub») или в приложении Harmony.
2. В Harmony: **Пульт → Файлы конфигурации — все модели… → Прочитать Harmony Hub по сети…**,
   ввести адрес, выбрать, куда сохранить файл `*.hub.json`.
3. Прислать этот файл.

Из командной строки:

```text
harmony-cli hub-read 192.168.1.50 MyHub.hub.json
```

Существующий файл не перезаписывается. Hub при чтении ничего не меняет: активности не
запускаются, команды не отправляются.

## Рутованный хаб: там есть ИК-коды

Ответ стокового хаба по сети ИК-кодов не содержит. Но сам хаб хранит их в своих
файлах ресурсов, и на рутованном хабе их можно забрать целиком:

- `/data/resources/DeviceList.json` — устройства и **все команды с ИК-кодами**;
- `/data/resources/ActivityList.json` — активности и роли устройств;
- `MapList.json`, `FunctionList.json`, `ProtocolList.json` — раскладки кнопок и протоколы.

Их отдаёт [Melodious](https://github.com/schlambos/harmony-hub-control) (`harmony-hub-control`),
установленный на рутованный хаб: `http://<hub-ip>:8080/export/devices`,
`/export/activities` или одним файлом `/export/bundle`. Для рутинга используется
отдельный инструмент `harmony-hub-root`; Harmony ничего не рутует и не прошивает.

Код команды лежит в поле `KeyCode` в виде `G:Toshiba 32 Bit:(0x20DF3AC5)(Repeat)():3`:
вид записи, имя протокола Logitech, код, признак повтора и число повторов. Для
Bluetooth-команд там код HID: `G:HID 16 Bit:()(07000027)():1`. `TransportType` различает
каналы: 1 — ИК, 32 — Bluetooth, 64 — устройство Home Assistant.

Эти файлы разбирает та же команда:

```text
harmony-cli hub-info DeviceList.json --commands
harmony-cli hub-info harmony-owner-bundle.json
harmony-cli hub-info MyHub.hub.json --irdb harmony.irdb
```

Конфигурация хаба переводится в общую модель редактора (`formats::toRemoteConfig`), поэтому
её показывает и обычная команда `harmony-cli info <файл>` — так же, как конфигурацию 900.
Переносятся устройства с командами, активности с ролями и входами. Экранные и аппаратные
кнопки лежат в `MapList.json`, который пока не разбирается.

`--irdb` ищет в нашей базе ИК-кодов модели, похожие на устройства без кодов — по
производителю и модели. Это подсказка для ручного сопоставления: команды пока
не переносятся. Соответствие имён протоколов Logitech нашим именам протоколов
не установлено: для этого нужны реальные файлы с кодами, проверенные на железе.

## Что в файле

JSON версии 1 (`format = harmony-hub-config`): время чтения UTC, `remoteId`, имя Hub,
версия прошивки, безопасные поля `connect.discoveryinfo` (имя, версия, protocolVersion)
и ответ `engine?config` без изменений: устройства (производитель, модель, тип, группы
команд с именами), активности (устройства, входы, кнопки), последовательности.

В файл **не попадают**: e-mail и accountId из ответа provisioning, hubId из discoveryinfo,
IP-адрес Hub. Названия устройств и активностей — те, что задал пользователь; их стоит
просмотреть перед отправкой.

**ИК-кодов в ответе Hub нет**, только имена команд. Для импорта в редактор их предстоит
подбирать по производителю и модели из базы ИК-кодов (`irdb`).

## Протокол

1. `POST /` на порт 8088 с телом `{"id ":1,"cmd":"setup.account?getProvisionInfo"}` и
   `Origin: http://sl.dhg.myharmony.com` → `data.activeRemoteId`.
2. `GET /?domain=svcs.myharmony.com&hubId=<remoteId>` с переходом на WebSocket (RFC 6455).
3. Текстовое сообщение `{"hubId":…,"hbus":{"cmd":"vnd.logitech.harmony/vnd.logitech.harmony.engine?config","id":…,"params":{"verb":"get","format":"json"}}}`;
   уведомления `connect.stateDigest?notify` и промежуточные ответы с кодом 100 пропускаются.
4. Необязательно `connect.discoveryinfo?get` — имя и версия прошивки.

Источники описания: клиент aioharmony (Home Assistant), совместимый режим harmony-ng
(`payload/source/hbus_server.c`, `tests/test_hbus.py`, MIT). Код Harmony написан заново
(`transport/src/hubConfigReader.cpp`), минимальный WebSocket-клиент встроен, новых
зависимостей нет.

## Ограничения

- Работает со стоковой прошивкой Hub и с harmony-ng при включённой «Logitech protocol
  compatibility (port 8088)». Старый XMPP-протокол (порт 5222) не используется: Logitech
  закрыла его в прошивке 4.15.206.
- Автономные Touch, Ultimate One и 950 без Hub так прочитать нельзя: им Hub не нужен, а по USB
  они конфигурацию не отдают. Их модель и прошивку показывает `harmony-cli transfer-info auto`.
- Разбор файлов (`hub-info`) есть; импорт в редактор и запись обратно пока не реализованы —
  сначала нужны реальные файлы с разных Hub. Запись на **стоковый** хаб локально невозможна
  в принципе: конфигурация приходит туда из облака Logitech. Запись реальна только на
  рутованный хаб, через API Melodious (`POST /import`, `/api/activity-save`).
- Ответ ограничен 32 МБ; таймаут ответа — 20 секунд на порцию данных.
