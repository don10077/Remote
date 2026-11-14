#pragma once

/**
 * @file gspmScreens.h
 * @brief Экраны (секция 6) и тач-зоны (секция 19) образа Harmony One.
 *
 * Описание формата — docs/harmony_one/gspm.md в afterglow:
 * @code
 *   секция 6   u16 число экранов, u8 0, u24 → трейлер × N
 *   трейлер    u8 флаг, u24 → заголовок, u16 число слотов, u24 → слот × N
 *   слот       7 байт: u8 шаблон, u24 → ключевой регистр, u24 → программа рисования
 *   регистр    u8 число записей, затем 4 байта: u8 код клавиши, u16 операнд, u8 категория
 *   секция 19  u8 число шаблонов, u24 → шаблон × N
 *   шаблон     u8 число зон, u24 → зона × N
 *   зона       u16 x, u16 ширина, u16 y, u16 высота, u8 тег, u24 → сама зона (замыкает), …
 * @endcode
 * Код клавиши зоны — `тег | 0x80`. Ось Y сенсора перевёрнута. Только чтение.
 * Программа рисования здесь не разбирается: хранится смещение.
 * Проверено на собранных по описанию данных, дампа One у нас нет.
 */

#include "harmony/core/error.h"
#include "harmony/core/formats/gspm.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace harmony::formats {

/// Запись ключевого регистра слота.
struct GspmKeyEntry {
    std::uint8_t keyCode = 0;
    std::uint16_t operand = 0;
    std::uint8_t category = 0;
};

/// Слот экрана (страница или область).
struct GspmScreenSlot {
    int templateIndex = 0;            ///< Номер шаблона тач-зон в секции 19
    std::size_t keyRegister = 0;      ///< Смещение ключевого регистра
    std::size_t program = 0;          ///< Смещение программы рисования
    std::vector<GspmKeyEntry> entries;
};

/// Экран: трейлер со слотами.
struct GspmScreen {
    std::size_t trailer = 0;
    std::uint8_t flag = 0;
    std::size_t header = 0;           ///< Смещение заголовка (в образе может быть вне границ — тогда 0)
    std::vector<GspmScreenSlot> slots;
};

/// Тач-зона шаблона.
struct GspmTouchZone {
    int x = 0, width = 0, y = 0, height = 0;
    std::uint8_t tag = 0;
    /// @return Код клавиши, который зона посылает: тег с установленным старшим битом.
    [[nodiscard]] int keyCode() const { return tag | 0x80; }
};

/// Шаблон тач-зон: touch[n] — шаблон номер n.
using GspmTouchTemplate = std::vector<GspmTouchZone>;

/// @brief Читает секцию 6. @return ErrorCode::Format — указатель вне образа, обрезанная структура.
[[nodiscard]] Result<std::vector<GspmScreen>> readGspmScreens(std::span<const std::uint8_t> payload,
                                                              const GspmImage& image);

/// @brief Читает секцию 19. @return ErrorCode::Format — указатель вне образа или зона не замыкается.
[[nodiscard]] Result<std::vector<GspmTouchTemplate>> readGspmTouchTemplates(
    std::span<const std::uint8_t> payload, const GspmImage& image);

} // namespace harmony::formats
