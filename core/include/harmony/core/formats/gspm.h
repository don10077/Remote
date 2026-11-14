#pragma once

/**
 * @file gspm.h
 * @brief Образ конфигурации Harmony One (архитектура 12): проверка структуры и индекс команд.
 *
 * Данные `.EZHex` пульта One — не ZIP, а бинарный образ `GSPM` с абсолютными адресами.
 * Формат восстановлен проектами afterglow и re-harmony (docs/harmony_one/gspm.md там),
 * здесь реализовано только чтение. Что разобрано:
 *  - внешняя структура: подпись, указатель конца, `LWJL`, `PTYY`, контрольная сумма XOR16;
 *  - мастер-индекс из 20 секций (секция 8 отсутствует);
 *  - секция 5 — двухуровневый индекс «устройство → команды» с записями ИК-команд.
 *
 * Имена устройств и команд лежат в экранах (глифы шрифтов) и здесь пока не читаются,
 * поэтому команды описаны номерами. Проверено только на синтетических образах по описанию
 * формата: настоящего дампа One у нас нет.
 */

#include "harmony/core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace harmony::formats {

/// Число записей мастер-индекса образа.
inline constexpr std::size_t kGspmSectionCount = 20;
/// Секция, которой в образах One нет.
inline constexpr std::size_t kGspmAbsentSection = 8;

/// Проверенная внешняя структура образа GSPM.
struct GspmImage {
    std::uint32_t base = 0;            ///< Адрес, по которому образ отображён во флеш (0x040000 у обычных)
    std::size_t checksumOffset = 0;    ///< Смещение двух байт контрольной суммы
    int buttonCount = 0;               ///< Байт после `LWJL`: число кнопок (55 у обычных образов)
    /// Смещения секций от начала образа; nullopt — секции нет.
    std::array<std::optional<std::size_t>, kGspmSectionCount> sections{};

    /// @return Сколько секций присутствует.
    [[nodiscard]] std::size_t sectionCount() const;
};

/// Диапазон секции: от её начала до начала следующей (или до контрольной суммы).
struct GspmRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

/// Команда из секции 5.
struct GspmCommand {
    int deviceOrdinal = 0;  ///< Номер устройства (старший байт ID команды)
    int commandOrdinal = 0; ///< Номер команды у устройства (младший байт ID)
    bool toggles = false;   ///< Режим 2: два набора сигналов (RC5/RC6 с битом переключения)
    std::size_t record = 0; ///< Смещение записи команды в образе
    std::size_t press = 0;  ///< Смещение сигнала нажатия
    std::optional<std::size_t> hold;    ///< Сигнал удержания
    std::optional<std::size_t> release; ///< Сигнал отпускания

    /// @return ID команды: `(устройство << 8) | команда`.
    [[nodiscard]] int id() const { return (deviceOrdinal << 8) | commandOrdinal; }
};

/// Устройства и команды секции 5.
struct GspmCommandIndex {
    /// commands[d] — команды устройства с номером d, в порядке индекса.
    std::vector<std::vector<GspmCommand>> devices;

    /// @return Всего команд.
    [[nodiscard]] std::size_t commandCount() const;
};

/**
 * @brief Проверяет образ GSPM и разбирает мастер-индекс.
 *
 * Адрес отображения выводится из указателя конца, а не берётся константой: у заводской
 * конфигурации прошивки он 0x2000, у обычных — 0x040000.
 * @param payload Данные .EZHex.
 * @return Образ; ErrorCode::Format — подпись, указатель конца, `LWJL`, `PTYY`, сумма
 *         или секции не сходятся.
 */
[[nodiscard]] Result<GspmImage> parseGspm(std::span<const std::uint8_t> payload);

/**
 * @brief Диапазон секции образа.
 * @return nullopt — номер вне 0..19 или секции нет.
 */
[[nodiscard]] std::optional<GspmRange> gspmSectionRange(const GspmImage& image, std::size_t index);

/**
 * @brief Читает индекс команд (секция 5), не выходя за границы образа.
 * @return Индекс; ErrorCode::Format — указатель вне образа или запись обрезана.
 */
[[nodiscard]] Result<GspmCommandIndex> readGspmCommands(std::span<const std::uint8_t> payload,
                                                        const GspmImage& image);

} // namespace harmony::formats
