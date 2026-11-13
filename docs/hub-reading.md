# Чтение конфигурации Harmony Hub по сети

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
- Импорт `*.hub.json` в редактор и запись обратно пока не реализованы — сначала нужны
  реальные файлы с разных Hub.
- Ответ ограничен 32 МБ; таймаут ответа — 20 секунд на порцию данных.
