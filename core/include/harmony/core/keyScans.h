#pragma once

/**
 * @file keyScans.h
 * @brief Скан-таблицы клавиш: скан-код → имя, по одному на семейство (и по модели, где
 *        семейство не отвечает на вопрос).
 *
 * В бинарных образах записан только скан-код клавиши (`key.tag & 63`): имён клавиш в файле
 * нет, тексты там — подписи экранных ячеек. Значит соответствие «код → имя» — знание
 * программы, и оно принадлежит семейству, а не модели: код `0x2C` — `Play` на 650
 * (архитектура 14), но `SoftRight4` на 885 (архитектура 8), код `0x1E` — `Play` на One
 * (архитектура 12). Поэтому общей таблицы кодов нет, а имена берутся из словаря
 * (`keyVocabulary`, K1) — таблица называет только каноническое имя.
 *
 * Источник имени — тот же, что у словаря (правило 8 [../key-names.md]), и он не
 * повторяется в записи таблицы: у имени один источник, он записан в словаре.
 *
 * Полнота таблицы (`complete`) — решение программы, а не свойство файла: false значит,
 * что отсутствия имени в таблице достаточно, чтобы сказать «такой клавиши у модели нет»
 * (K4, C1). Для архитектур 2, 3, 7, 9, 10, 11, 15 и 17 таблиц нет вовсе — там список
 * клавиш модели дают профиль, фактическая конфигурация или таблица надписей по модели.
 *
 * `UnknownKeyNN` здесь не появляется: у неподтверждённого кода имени нет (правило 6
 * [../key-names.md]), для отображения используется пометка `scanPlaceholder()`.
 */

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace harmony {

struct KeyScan {
    unsigned scan;             ///< Скан-код клавиши (`key.tag & 63`), 0x01…0x3F
    std::string_view name;     ///< Каноническое имя словаря — не алиас и не пометка
};

struct KeyScanTable {
    int architecture;                 ///< Семейство прошивки: 8, 12, 14, 16, 18
    std::string_view model;           ///< Пусто — таблица семейства; иначе — таблица модели
    bool complete;                    ///< Таблица подтверждена целиком (правило полноты, K4)
    std::span<const KeyScan> entries; ///< Записи по возрастанию скан-кода
};

/// @return Все таблицы — по возрастанию архитектуры, внутри семейства сначала таблица модели.
[[nodiscard]] std::span<const KeyScanTable> keyScanTables();

/**
 * @brief Таблица кодов для модели: сначала таблица модели, затем таблица семейства.
 * @param architecture Семейство прошивки (`RemoteModel::architecture`, M1).
 * @param modelId Id модели; регистр и суффикс `i` не важны (`1100i` → `1100`).
 * @return Таблицу или nullptr — таблицы для этого семейства или модели нет.
 */
[[nodiscard]] const KeyScanTable* keyScanTable(int architecture, std::string_view modelId = {});

/**
 * @brief Имя клавиши по коду — по таблице модели, иначе по таблице семейства.
 * @param architecture Семейство прошивки.
 * @param modelId Id модели; пусто — только таблица семейства.
 * @param scan Скан-код (`key.tag & 63`).
 * @return Каноническое имя или nullopt — кода `0x00`, кода выше 0x3F или кода нет в таблице.
 */
[[nodiscard]] std::optional<std::string_view> keyNameForScan(int architecture,
                                                             std::string_view modelId,
                                                             unsigned scan);

/**
 * @brief Пометка для отображения кода, имени которого в таблице нет (`Scan2C`).
 * @param scan Скан-код.
 * @return «Scan» + HEX в верхнем регистре, минимум две цифры. Это не имя: у неподтверждённого
 *         кода имени нет (правило 6 [../key-names.md]).
 */
[[nodiscard]] std::string scanPlaceholder(unsigned scan);

/**
 * @brief Полна ли таблица для модели: отсутствие имени значит «такой клавиши у модели нет».
 * @param architecture Семейство прошивки.
 * @param modelId Id модели; регистр и суффикс `i` не важны.
 * @return true — таблица подтверждена целиком; false — таблицы нет или она неполная (K4, C1).
 */
[[nodiscard]] bool keyScanTableComplete(int architecture, std::string_view modelId = {});

} // namespace harmony
