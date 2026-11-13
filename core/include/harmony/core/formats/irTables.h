#pragma once

/**
 * @file irTables.h
 * @brief Двоичные таблицы ИК-данных конфигурации: IrProto.bin и SsIr.bin.
 *
 * Обе — таблицы записей переменной длины с общим устройством:
 * @code
 *   01 01 05 00 00 | 01 | число записей (16 бит) | смещения записей (16 бит × N) | данные
 * @endcode
 * Все числа — little endian. Смещения отсчитываются от байта сразу после
 * пятибайтового заголовка.
 *
 * - IrProto.bin — протоколы (несущая, тайминги). Команда устройства ссылается
 *   на протокол номером: <Protocol>N</Protocol>, и тот же номер стоит в первых
 *   двух байтах её <Code>. Файл обёрнут: CRC32 данных (4 байта), размер данных
 *   (2 байта), 00 00. Этот CRC32 записан в XML как хеш протоколов.
 *   Внутри протокола есть смещения на его же части, отсчитанные от начала
 *   таблицы. При чтении они пересчитываются от начала записи, поэтому протокол
 *   можно переносить между файлами и сравнивать побайтно.
 * - SsIr.bin — «сырые» выученные коды (несущая и длительности). Команда
 *   ссылается так: <Protocol>-1</Protocol>, <Code>0xFFFF</Code> + номер кода (16 бит).
 *   Обёртки с CRC нет.
 */

#include "harmony/core/error.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::formats {

/// Имена файлов таблиц в папке конфигурации.
inline constexpr std::string_view kIrProtoFileName = "IrProto.bin";
inline constexpr std::string_view kSsIrFileName = "SsIr.bin";

/// Байты одной записи или файла целиком.
using Bytes = std::vector<std::uint8_t>;

/**
 * @brief Таблица записей (протоколов или сырых кодов).
 *
 * Номер записи — её индекс в entries; на него ссылаются команды устройств.
 */
struct IrTable {
    std::vector<Bytes> entries;

    /**
     * @brief Номер записи с такими же байтами; если такой нет — добавляет её.
     *
     * Одинаковые протоколы у разных устройств хранятся один раз.
     * @param entry Запись (для протоколов — в перемещаемом виде, как после чтения).
     * @return Номер записи.
     */
    std::size_t intern(const Bytes& entry);
};

/**
 * @brief Помещается ли таблица в файл.
 *
 * Смещения записей, указатели внутри протоколов и размер в обёртке IrProto.bin —
 * 16-битные, поэтому данные таблицы (от заголовка до конца) не длиннее 0xFFFF байт.
 * Иначе writeIrProto()/writeSsIr() молча записали бы обрезанные смещения.
 */
[[nodiscard]] bool fitsOffsets(const IrTable& table);

/**
 * @brief CRC32 (как в zlib: полином 0xEDB88320, начальное и конечное XOR 0xFFFFFFFF).
 * @param data Данные.
 */
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data);

/**
 * @brief Разбирает IrProto.bin.
 * @param file Содержимое файла.
 * @return Таблица протоколов (смещения внутри протоколов — от начала записи);
 *         ErrorCode::Format — неверный размер, CRC или заголовок.
 */
[[nodiscard]] Result<IrTable> parseIrProto(std::span<const std::uint8_t> file);

/**
 * @brief Собирает IrProto.bin; разобранный файл собирается обратно байт в байт.
 * @param table Таблица протоколов.
 * @return ErrorCode::Format — таблица не помещается в 16-битные смещения (fitsOffsets())
 *         или указатели записи выходят за её пределы.
 */
[[nodiscard]] Result<Bytes> buildIrProto(const IrTable& table);

/**
 * @brief То же, что buildIrProto(), для таблиц, уже прошедших проверку (из parseIrProto()).
 * @return Содержимое файла; пусто — таблицу записать нельзя (причину скажет buildIrProto()).
 */
[[nodiscard]] Bytes writeIrProto(const IrTable& table);

/**
 * @brief Хеш протоколов для XML (ProtocolCacheHash, <Protocols><Hash>): CRC32 из обёртки IrProto.bin.
 * @param irProtoFile Содержимое IrProto.bin (как из writeIrProto()).
 * @return Строка вида «0x8595F4FF».
 */
[[nodiscard]] std::string protocolHash(std::span<const std::uint8_t> irProtoFile);

/**
 * @brief Разбирает SsIr.bin.
 * @param file Содержимое файла.
 * @return Таблица сырых кодов; ErrorCode::Format — неверный заголовок или смещения.
 */
[[nodiscard]] Result<IrTable> parseSsIr(std::span<const std::uint8_t> file);

/**
 * @brief Собирает SsIr.bin; разобранный файл собирается обратно байт в байт.
 * @param table Таблица сырых кодов.
 */
[[nodiscard]] Bytes writeSsIr(const IrTable& table);

} // namespace harmony::formats
