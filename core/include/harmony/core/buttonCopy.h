#pragma once

/**
 * @file buttonCopy.h
 * @brief Копирование кнопок активности в другие активности.
 *
 * Раскладка целиком (аппаратные и экранные кнопки) или одна кнопка — в
 * несколько активностей сразу. Кнопка копируется как есть (подпись, значок,
 * команда); если её устройство в активности не включается, она бы не
 * сработала — такая кнопка пропускается.
 */

#include <string>
#include <vector>

namespace harmony {

/// Что и куда скопировать.
struct ButtonCopy {
    std::string fromActivity;              ///< Откуда
    std::vector<std::string> toActivities; ///< Куда
    bool hard = true;                      ///< Аппаратные кнопки
    bool soft = true;                      ///< Экранные кнопки
    std::vector<std::string> hardKeys;     ///< Только эти аппаратные («Menu»); пусто — все
    std::vector<int> softPositions;        ///< Только эти экранные (места); пусто — все
    /// true — занятые кнопки и места заменяются; false — заняты: аппаратная
    /// остаётся прежней, экранная встаёт на первое свободное место.
    bool replace = true;
};

/// Что получилось.
struct ButtonCopyReport {
    int activities = 0; ///< Активностей изменено
    int hard = 0;       ///< Аппаратных кнопок назначено
    int soft = 0;       ///< Экранных кнопок поставлено
    int kept = 0;       ///< Не заменено: кнопка уже занята (replace = false)
    int skipped = 0;    ///< Пропущено: устройство кнопки в активности не включается
};

} // namespace harmony
