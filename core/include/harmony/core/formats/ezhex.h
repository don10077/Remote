#pragma once

/**
 * @file ezhex.h
 * @brief Контейнер .EZHex: так конфигурация передаётся на пульт и снимается с него.
 *
 * @code
 *   <?xml version="1.0"?>\r\n<INFORMATION> … </INFORMATION>\r\n   ← заголовок, строки CRLF
 *   PK…                                                         ← ZIP с деревом файлов пульта
 * @endcode
 * В заголовке — для какого пульта конфигурация (<INTENDEDVERSION>), размер
 * данных (<BINARYDATASIZE>) и контрольная сумма (<CHECKSUM>: 0x69, XOR со всеми
 * байтами данных, записана десятичным числом). Резервные копии concordance
 * (`--dump-config`) и afterglow — в этом же формате.
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/irTables.h"

#include <cstdint>
#include <span>
#include <string>

namespace harmony::formats {

/**
 * @brief Чей это пульт: архитектура и модель (skin), как их сообщает сам пульт.
 *
 * Harmony 900 — архитектура 15, skin 61. Запись конфигурации на пульт другой
 * модели может сделать его неработоспособным, поэтому перед записью
 * идентичность сверяется.
 */
struct RemoteIdentity {
    int architecture = 15;        ///< <PROTOCOL>
    int skin = 61;                ///< <SKIN> — модель
    std::string flash = "0x01:0x49"; ///< <FLASH> — микросхема памяти
    std::string board = "0.1.0";  ///< <BOARD> — ревизия платы
    int softwareType = 0;         ///< <SOFTWARETYPE>

    /// @return true, если это та же модель пульта (архитектура и skin).
    [[nodiscard]] bool sameModel(const RemoteIdentity& other) const {
        return architecture == other.architecture && skin == other.skin;
    }
};

/// Разобранный .EZHex.
struct EzHex {
    std::string header;      ///< Заголовок до конца строки после </INFORMATION> (как в файле)
    Bytes payload;           ///< Данные (ZIP с деревом файлов)
    RemoteIdentity identity; ///< Для какого пульта (из <INTENDEDVERSION>)
};

/**
 * @brief Контрольная сумма данных для <CHECKSUM>.
 * @param payload Данные.
 */
[[nodiscard]] std::uint8_t ezHexChecksum(std::span<const std::uint8_t> payload);

/**
 * @brief Разбирает .EZHex и проверяет размер и контрольную сумму.
 * @return Контейнер; ErrorCode::Format — нет заголовка или данные не сходятся с ним.
 */
[[nodiscard]] Result<EzHex> parseEzHex(std::span<const std::uint8_t> file);

/**
 * @brief Заголовок, какой пишут concordance и программы Logitech, для данных и пульта.
 * @param identity Пульт.
 * @param payload Данные (для размера и контрольной суммы).
 */
[[nodiscard]] std::string makeEzHexHeader(const RemoteIdentity& identity,
                                          std::span<const std::uint8_t> payload);

/**
 * @brief Собирает .EZHex.
 *
 * Заголовок берётся из @p ezhex (остальные поля сохраняются как были), в нём
 * обновляются размер и контрольная сумма; пустой заголовок создаётся заново.
 * @return Содержимое файла; ErrorCode::Format — в заголовке нет <BINARYDATASIZE>
 * или <CHECKSUM> (тогда файл получился бы с устаревшими значениями).
 */
[[nodiscard]] Result<Bytes> writeEzHex(const EzHex& ezhex);

} // namespace harmony::formats
