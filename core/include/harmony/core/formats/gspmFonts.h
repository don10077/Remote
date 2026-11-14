#pragma once

/**
 * @file gspmFonts.h
 * @brief Шрифты Harmony One (секция 7 образа GSPM) и декодирование глифов в маски.
 *
 * Подписи устройств, команд и кнопок на экранах One — не строки, а нарисованные глифы.
 * Чтобы их прочитать, нужно сначала разобрать шрифты (docs/harmony_one/gspm.md в afterglow):
 * @code
 *   секция 7   u16 число шрифтов, u24 → шрифт × N
 *   шрифт      u8 высота, затем один из двух заголовков:
 *                обычный образ — u16 число глифов (по +1),
 *                заводской     — число глифов в байте +2;
 *              далее u24 → глиф × N (0 — глифа в шрифте нет)
 *   глиф       u8 ширина, поток строк: 0x80 — конец строки, 0x00 — конец глифа,
 *              старший бит — пропустить N прозрачных пикселей,
 *              иначе — N пикселей RGB565 (big endian), каждый рисует
 * @endcode
 * Слот глифа — его номер в репертуаре шрифта, считая с 1. Какому символу соответствует
 * номер, зависит от размера репертуара; здесь коды не сопоставляются с символами.
 *
 * Только чтение. Проверено на собранных по описанию данных, дампа One у нас нет.
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/gspm.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace harmony::formats {

/// Глиф как маска: 1 — пиксель рисуется, 0 — прозрачный.
struct GspmGlyph {
    int width = 0;
    int height = 0;
    std::vector<std::vector<std::uint8_t>> rows; ///< height строк по width значений 0/1
    std::size_t encodedSize = 0;                 ///< Сколько байт занимает поток глифа

    /// @return Отпечаток формы (FNV-1a от ширины, высоты и строк): одинаковая форма — одинаковый отпечаток.
    [[nodiscard]] std::uint64_t fingerprint() const;
};

/// Шрифт секции 7.
struct GspmFont {
    std::size_t offset = 0;      ///< Смещение заголовка шрифта в образе
    int height = 0;              ///< Высота, пиксели
    bool byteCount = false;      ///< Число глифов взято из байта +2 (заводской образ)
    /// Слот с номером n — glyphs[n - 1]; nullopt — глифа в этом образе нет.
    std::vector<std::optional<GspmGlyph>> glyphs;

    /// @return Сколько глифов реально есть.
    [[nodiscard]] std::size_t presentCount() const;
};

/**
 * @brief Декодирует один глиф.
 * @param payload Образ.
 * @param offset Смещение глифа от начала образа.
 * @return Глиф; ErrorCode::Format — выход за образ, пустой ран, неровные строки, нет терминатора,
 *         ширина не совпала с заявленной.
 */
[[nodiscard]] Result<GspmGlyph> decodeGspmGlyph(std::span<const std::uint8_t> payload, std::size_t offset);

/**
 * @brief Читает шрифты секции 7 по границам, заданным вызывающим (удобно для тестов).
 * @param base Адрес отображения образа.
 * @param begin Начало секции 7.
 * @param end Конец секции 7.
 */
[[nodiscard]] Result<std::vector<GspmFont>> readGspmFontsAt(std::span<const std::uint8_t> payload,
                                                            std::uint32_t base, std::size_t begin,
                                                            std::size_t end);

/**
 * @brief Читает все шрифты образа.
 * @return Шрифты; ErrorCode::Format — секции 7 нет, число глифов не выводится, указатель вне образа.
 */
[[nodiscard]] Result<std::vector<GspmFont>> readGspmFonts(std::span<const std::uint8_t> payload,
                                                          const GspmImage& image);

} // namespace harmony::formats
