#pragma once

/**
 * @file remoteCapture.h
 * @brief Конфигурация, снятая с пульта для пересылки: один JSON-файл `*.remote.json`.
 *
 * @code
 * {
 *   "format": "harmony-remote-capture", "version": 1,
 *   "createdUtc": "2026-11-16T12:30:00Z", "program": "Harmony 0.9.0",
 *   "remote": { "id": "665", "model": "Harmony 665", "architecture": 14, "skin": 99,
 *               "protocol": 14, "softwareType": 0, "firmware": "2.2",
 *               "flash": "0x01:0x49", "board": "0.1.0" },
 *   "data":   { "container": "ezhex", "size": 123456, "sha256": "…", "base64": "…" }
 * }
 * @endcode
 * В `data` — байты в том виде, в каком их отдаёт чтение: `ezhex` — файл .EZHex
 * (заголовок и данные), `mh` — ZIP пультов MH (Description.xml и Result.EzHex).
 * Старый файл `*.hid.json` (`format = harmony-hid-backup`) читается как `raw`:
 * исходный образ памяти без контейнера; такие файлы больше не создаются.
 *
 * Разбор проверяет целостность (размер и SHA-256), но не содержимое контейнера:
 * это делает транспорт (inspectCapture), которому доступен ZIP пультов MH.
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/ezhex.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace harmony::formats {

/// Что лежит в данных снятого файла.
enum class CaptureContainer {
    EzHex, ///< Файл .EZHex
    Mh,    ///< ZIP пультов MH
    Raw,   ///< Образ памяти без контейнера (только старые *.hid.json)
};

/// Снятая с пульта конфигурация и сведения о снятии.
struct RemoteCapture {
    std::string createdUtc;   ///< Когда снято, ISO 8601 UTC («2026-11-16T12:30:00Z»)
    std::string program;      ///< Чем снято («Harmony 0.9.0»); пусто при записи — текущая версия
    std::string modelId;      ///< id модели из каталога («665»); пусто — неизвестна
    std::string model;        ///< Как модель назвал пульт («Harmony 665»)
    RemoteIdentity identity;  ///< Архитектура, skin, flash, плата, тип ПО
    int protocol = -1;        ///< Протокол обмена (PROTOCOL в EZHex); -1 — неизвестен
    std::string firmware;     ///< Версия прошивки («2.2»)
    CaptureContainer container = CaptureContainer::EzHex; ///< Вид данных
    Bytes data;               ///< Данные как сняты
    std::uint32_t rawBase = 0; ///< Только Raw: адрес начала образа в памяти пульта
    std::string identityHex;  ///< Только Raw: исходный ответ пульта на запрос версии, hex
    bool legacy = false;      ///< Прочитан старый *.hid.json
};

/// Расширение файла снятой конфигурации.
inline constexpr std::string_view kRemoteCaptureExtension = ".remote.json";

/**
 * @brief Похоже ли содержимое на снятый файл (новый или старый *.hid.json).
 * @param json Содержимое файла.
 * @return true — JSON-объект с подходящим полем format; версию и данные не проверяет.
 */
[[nodiscard]] bool isRemoteCapture(std::string_view json);

/**
 * @brief Разбирает снятый файл и проверяет целостность данных.
 * @param json Содержимое `*.remote.json` или старого `*.hid.json`.
 * @return Снятая конфигурация; ErrorCode::Format — не тот файл, нет полей, Base64 испорчен,
 *         размер или SHA-256 не сходятся; ErrorCode::Unsupported — другая версия формата.
 */
[[nodiscard]] Result<RemoteCapture> parseRemoteCapture(std::string_view json);

/**
 * @brief Собирает `*.remote.json`: размер и SHA-256 считаются по данным.
 * @param capture Снятая конфигурация; container Raw не пишется.
 * @return Текст JSON; ErrorCode::Unsupported — Raw (старый формат только читается),
 *         ErrorCode::Format — нет данных.
 */
[[nodiscard]] Result<std::string> writeRemoteCapture(const RemoteCapture& capture);

/**
 * @brief Время в виде ISO 8601 UTC с точностью до секунды («2026-11-16T12:30:00Z»).
 * @param time Момент.
 */
[[nodiscard]] std::string utcTimestamp(std::chrono::system_clock::time_point time);

/**
 * @brief Имя файла по умолчанию: «Harmony-665-20261116-1530.remote.json».
 * @param modelId id модели; пусто — «remote».
 * @param localTime Местное время снятия: год, месяц, день, час, минута.
 */
[[nodiscard]] std::string captureFileName(std::string_view modelId, int year, int month, int day, int hour,
                                          int minute);

/// @return Имя вида данных для JSON («ezhex», «mh», «raw»).
[[nodiscard]] std::string_view containerName(CaptureContainer container);

} // namespace harmony::formats
