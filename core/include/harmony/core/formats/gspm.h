#pragma once

/**
 * @file gspm.h
 * @brief Образ конфигурации Harmony One (архитектура 12): проверка структуры и индекс команд.
 *
 * Данные `.EZHex` пульта One — не ZIP, а бинарный образ `GSPM` с абсолютными адресами.
 * Общая рамка (признак, число слотов, таблица `{spare, u24}`, маркеры, XOR16) разбирается
 * parseContainer() из container.h; здесь только то, что у One своё: граница защищённой
 * памяти и содержимое секций. Формат восстановлен проектами afterglow и re-harmony
 * (docs/harmony_one/gspm.md там), здесь реализовано только чтение. Что разобрано:
 *  - таблица секций из 22 сырых слотов: 20 базовых, пустой вставной 8 и секция 18,
 *    которой нет у других архитектур (базовые 18 и 19 пусты всегда);
 *  - секция 5 — двухуровневый индекс «устройство → команды» с записями ИК-команд.
 *
 * Номера у gspmSectionRange() — базовые слоты 0..19: в таблице One они лежат на сырых
 * слотах gspmRawSlot(b). У 0..7 номера совпадают, поэтому команды (5), экраны (6) и
 * шрифты (7) читаются под своими номерами, а тач-зоны — под базовым 17, то есть на
 * сыром слоте 19.
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

/// Базовых слотов (общих для всех архитектур с этой раскладкой) 20.
inline constexpr std::size_t kGspmBaseSlotCount = 20;
/// Сырых слотов таблицы секций у One 22: 20 базовых + вставной пустой + ещё одна секция.
inline constexpr std::size_t kGspmRawSlotCount = 22;
/// Пустые сырые слоты: вставной 8 и сырые 20 и 21 — это базовые 18 и 19, пустые всегда.
inline constexpr std::array<std::size_t, 3> kGspmEmptySlots{8, 20, 21};

/**
 * @brief Базовый слот 0..19 → сырой слот таблицы One.
 *
 * arch 12 вставляет пустой слот в 8 и ещё одну секцию в 18, поэтому базовый слот `b`
 * лежит на сыром `b`, `b + 1` при `b >= 8` и `b + 2` при `b >= 17` — карта слотов
 * arch 12 в reference/config-format/container.md проекта harmony-explorations (MIT).
 */
[[nodiscard]] constexpr std::size_t gspmRawSlot(std::size_t baseSlot) {
    return baseSlot + (baseSlot >= 8 ? std::size_t{1} : std::size_t{0})
           + (baseSlot >= 17 ? std::size_t{1} : std::size_t{0});
}

/// Проверенная внешняя структура образа GSPM.
struct GspmImage {
    std::uint32_t base = 0;            ///< Адрес, по которому образ отображён во флеш (0x040000 у обычных)
    std::size_t checksumOffset = 0;    ///< Смещение двух байт контрольной суммы
    int buttonCount = 0;               ///< Байт после `LWJL`: число кнопок (55 у обычных образов)
    /// Смещения секций по сырым слотам таблицы; nullopt — слота нет.
    std::array<std::optional<std::size_t>, kGspmRawSlotCount> sections{};

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
 * @brief Проверяет образ GSPM и разбирает таблицу секций One.
 *
 * Рамку проверяет parseContainer(); отдельно сверяется, что это именно arch 12, а не
 * 600–700 с тем же признаком `GSPM`. Адрес отображения выводится из указателя конца, а
 * не берётся константой: у заводской конфигурации прошивки он 0x2000, у обычных — 0x040000.
 * @param payload Данные .EZHex.
 * @return Образ; ErrorCode::Format — рамка не сходится, это не arch 12, заполнен слот,
 *         который архитектура оставляет пустым, или секций не 19.
 */
[[nodiscard]] Result<GspmImage> parseGspm(std::span<const std::uint8_t> payload);

/**
 * @brief Диапазон секции образа по базовому слоту 0..19 (он же номер у слотов 0..7).
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
