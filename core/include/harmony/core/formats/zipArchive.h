#pragma once

/**
 * @file zipArchive.h
 * @brief ZIP-архив с сохранением всех метаданных записей (для .EZHex).
 *
 * Пульт распаковывает конфигурацию на свою файловую систему, поэтому ему важен
 * не просто «правильный ZIP», а тот, что делали программы Logitech: порядок
 * записей, права Unix, сжатие или хранение, служебные поля Info-ZIP. Поэтому
 * здесь записи хранятся как есть (сжатые данные и все поля заголовков), и
 * неизменённый архив собирается обратно байт в байт. Меняется только то, что
 * явно заменили через setContent().
 *
 * Поддерживается обычный ZIP без шифрования, многотомности, ZIP64 и
 * дескрипторов данных — именно такие архивы делает пульт.
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/irTables.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace harmony::formats {

/// Запись архива: файл или папка (имя оканчивается на «/»).
struct ZipEntry {
    std::string name;                  ///< Путь внутри архива («userconfig/IrProto.bin»)
    std::uint16_t versionMadeBy = 0x0317; ///< Старший байт 3 — Unix, младший — версия ZIP (2.3)
    std::uint16_t versionNeeded = 20;  ///< 20 — нужен deflate, 10 — хранение
    std::uint16_t flags = 0;
    std::uint16_t method = 8;          ///< 8 — deflate, 0 — без сжатия
    std::uint16_t dosTime = 0;
    std::uint16_t dosDate = 0;
    std::uint32_t crc = 0;             ///< CRC32 распакованных данных
    std::uint32_t size = 0;            ///< Размер распакованных данных
    std::uint16_t internalAttributes = 0;
    std::uint32_t externalAttributes = 0; ///< Старшие 16 бит — права Unix (0100775 и т. п.)
    Bytes localExtra;                  ///< Служебные поля локального заголовка
    Bytes centralExtra;                ///< Служебные поля в оглавлении
    Bytes comment;
    Bytes data;                        ///< Данные в том виде, как лежат в архиве (сжатые)

    /// @return Права Unix (например, 0100775 — исполняемый файл).
    [[nodiscard]] std::uint32_t unixMode() const { return externalAttributes >> 16; }

    /// Задаёт права Unix (младшие биты атрибутов DOS сохраняются).
    void setUnixMode(std::uint32_t mode) {
        externalAttributes = (mode << 16) | (externalAttributes & 0xFFFFU);
    }
};

/// ZIP-архив: записи в порядке файла и комментарий архива.
struct ZipArchive {
    std::vector<ZipEntry> entries;
    Bytes comment;

    /// @return Запись по имени или nullptr.
    [[nodiscard]] ZipEntry* find(const std::string& name);
    [[nodiscard]] const ZipEntry* find(const std::string& name) const;
};

/**
 * @brief Разбирает ZIP.
 *
 * Размеры из заголовков не доверяем: запись больше 64 МиБ или архив больше
 * 256 МиБ распакованных данных отвергаются (защита от zip-bomb).
 * @return Архив; ErrorCode::Format — не ZIP, неподдерживаемый вариант или слишком
 * большой архив.
 */
[[nodiscard]] Result<ZipArchive> readZip(std::span<const std::uint8_t> bytes);

/// Собирает ZIP: локальные заголовки с данными подряд, затем оглавление.
[[nodiscard]] Bytes writeZip(const ZipArchive& archive);

/**
 * @brief Распакованное содержимое записи.
 *
 * Для записей без сжатия (метод 0) данные тоже сверяются с CRC и размером.
 * @return Данные; ErrorCode::Format — неизвестный метод сжатия или испорченные данные.
 */
[[nodiscard]] Result<Bytes> entryContent(const ZipEntry& entry);

/**
 * @brief Заменяет содержимое записи.
 *
 * Метод записи сохраняется (сжатая остаётся сжатой), CRC и размеры
 * пересчитываются. Если содержимое не изменилось, запись не трогается —
 * так неизменённый архив остаётся байт в байт прежним.
 * @return ErrorCode::Format, если не удалось распаковать прежнее содержимое.
 */
[[nodiscard]] Result<void> setContent(ZipEntry& entry, std::span<const std::uint8_t> content);

/**
 * @brief Новая запись в духе пульта: служебные поля Info-ZIP с временем, владелец root.
 * @param name Путь в архиве (папка — с «/» на конце).
 * @param mode Права Unix (0100775, 040775, ...).
 * @param content Содержимое (у папки — пусто).
 * @param unixTime Время изменения (секунды Unix).
 * @param compress true — deflate (если это действительно уменьшает размер), false — хранение.
 */
[[nodiscard]] ZipEntry makeEntry(const std::string& name, std::uint32_t mode,
                                 std::span<const std::uint8_t> content, std::int64_t unixTime,
                                 bool compress = true);

} // namespace harmony::formats
