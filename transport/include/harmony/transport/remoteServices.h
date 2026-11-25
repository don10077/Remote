#pragma once

/**
 * @file remoteServices.h
 * @brief Службы Harmony 900 помимо протокола конфигурации: настройки и консоль.
 *
 * Пульт — компьютер на QNX, и по USB-сети (169.254.1.2) отвечают не только
 * на порту 3074:
 *   - 80 — HTTP 1.0 (служба настроек): GET /system/<имя> возвращает значение
 *     в обёртке <Event>…<Value>…</Value>…</Event>, POST с голым значением в теле
 *     меняет его. Запрос — HTTP/1.0 с CRLF; POST обёртки Event сохранил бы саму обёртку.
 *   - 23 — telnet, вход root / ethanol (одинаков на всех 900, у 1000 — cognac).
 *     Системный журнал — команда sloginfo; есть и /fs/etfs/scratch/log.txt.
 *     Оболочка урезанная: нет grep, awk, sed, find — фильтровать на нашей стороне.
 *
 * Источники: harmony900tools (doc/general.md) и afterglow (remote_shell.py,
 * tools/remote_settings.py), оба проверены на пульте.
 */

#include "harmony/core/error.h"
#include "harmony/transport/channel.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace harmony::transport {

/// Порт HTTP-службы настроек.
inline constexpr std::uint16_t kSettingsPort = 80;
/// Порт telnet.
inline constexpr std::uint16_t kTelnetPort = 23;
/// Порт канала событий HAO: RF, активности, тесты устройств.
inline constexpr std::uint16_t kEventPort = 1100;

/// Учётная запись telnet Harmony 900 (не секрет: одинакова на всех пультах модели).
inline constexpr std::string_view kTelnetUser = "root";
inline constexpr std::string_view kTelnetPassword = "ethanol";

/**
 * @brief Значение настройки на подключённом пульте.
 * @param host Адрес пульта.
 * @param endpoint Адрес настройки: «/system/backlightlevel».
 * @param factory Как подключаться; пусто — TCP к host:80 (в тестах — подставной канал).
 * @return Значение; ErrorCode::Io — пульт не отвечает.
 */
[[nodiscard]] Result<std::string> readRemoteSetting(const std::string& host, std::string_view endpoint,
                                                    const ChannelFactory& factory = {});

/**
 * @brief Меняет настройку на подключённом пульте (сразу, без перезапуска).
 * @return ErrorCode::Io — пульт не отвечает; ErrorCode::Remote — пульт ответил ошибкой
 *         или ответ без строки статуса HTTP (не подтвердил).
 */
[[nodiscard]] Result<void> writeRemoteSetting(const std::string& host, std::string_view endpoint,
                                              std::string_view value, const ChannelFactory& factory = {});

/**
 * @brief Открывает на пульте окно сопряжения модуля Z-Wave (RF:ReadyForRF, затем RF:AddReceiver).
 *
 * Harmony 900 включает свой радиомодуль сам. У моделей с Z-Wave окно открывает программа:
 * сообщения уходят в канал событий (порт 1100), после чего пользователь переводит модуль
 * в режим сопряжения. MAC модуль сообщает позже в XmlUserRfSetting.xml, поэтому после
 * успеха конфигурацию читают заново.
 *
 * Вызывающего кода в интерфейсе пока нет (решение владельца 25.11.2026): у родного софта
 * сначала прошиваются пульт и расширитель, и только после прошивки расширитель
 * переподключается с пульта; первый раз его включает программа. Порядок прошивки в проекте
 * не реализован, поэтому «Настройки пульта» этого окна не предлагают. Протокол ниже
 * проверен тестами и готов к этому шагу.
 * @param host Адрес пульта.
 * @param wait Сколько ждать события NewReceiversFound; 0 — не ждать его (подтверждение
 *        готовности перед включением всё равно ждётся до 500 мс).
 * @param factory Как подключаться; пусто — TCP к host:1100 (в тестах — подставной канал).
 * @return true — пульт сообщил о найденном модуле; false — за @p wait не сообщил;
 *         ErrorCode::Io — канал не открылся или закрылся.
 */
[[nodiscard]] Result<bool> startExtenderPairing(const std::string& host,
                                                std::chrono::milliseconds wait = std::chrono::seconds(45),
                                                const ChannelFactory& factory = {});

/**
 * @brief Выполняет команду в консоли пульта (telnet) и возвращает её вывод.
 * @param host Адрес пульта.
 * @param command Команда оболочки QNX.
 * @param timeout Сколько ждать вывода.
 * @param factory Как подключаться; пусто — TCP к host:23.
 * @return Вывод без эха команды; ErrorCode::Io — нет связи, ErrorCode::Remote — вход не удался
 *         или команда не завершилась за @p timeout (обрезанный вывод не возвращается).
 */
[[nodiscard]] Result<std::string> runRemoteShell(const std::string& host, std::string_view command,
                                                 std::chrono::milliseconds timeout = std::chrono::seconds(20),
                                                 const ChannelFactory& factory = {});

/**
 * @brief Системный журнал пульта (sloginfo): нажатия, отправленные ИК-команды, сбои.
 *
 * Именно его стоит смотреть, когда пульт отверг конфигурацию или перезагрузился.
 */
[[nodiscard]] Result<std::string> readRemoteLog(const std::string& host, const ChannelFactory& factory = {});

/// Значение из ответа службы настроек: содержимое <Value>, иначе тело ответа.
[[nodiscard]] std::string settingValue(std::string_view response);

} // namespace harmony::transport
