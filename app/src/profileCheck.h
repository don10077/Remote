#pragma once

/**
 * @file profileCheck.h
 * @brief C2: места экранных кнопок по профилю модели.
 *
 * Ядро сообщает только то, что видно в конфигурации (`Position`): содержимое профиля
 * оно не читает. Страницу и место считает приложение, потому что места задаёт экран
 * профиля (`grid` и ячейки `ScreenSlot`). Отрисовка использует тот же счёт — правило V6
 * в [10-visual-profile.md].
 *
 * Проблемы профиля — отдельная группа `Problem::Where::Profile`, они добавляются
 * в конец результата `ConfigDocument::check()`. Подсказка «page N, place M» —
 * `Problem::hint`, которое заполняет приложение.
 */

#include "harmony/core/configCheck.h"

#include <QJsonArray>
#include <QJsonObject>
#include <vector>

namespace harmony::app {

/**
 * @brief Дополняет результат проверки проблемами профиля и подсказкой страницы.
 * @param problems результат `ConfigDocument::check()`: проблемы профиля добавляются
 *        в конец, подсказка страницы — к существующим проблемам с местом кнопки.
 * @param remote описание модели (снимок `remoteResources::remote`); пустой — профиля нет.
 */
void profileCheck(std::vector<Problem>& problems, const QJsonObject& remote);

/**
 * @brief Экран профиля, где показываются экранные кнопки: роль и число мест.
 * @param remote описание модели.
 * @return 0 — экрана этого режима нет или в нём нет мест для команд.
 */
[[nodiscard]] int commandSlots(const QJsonObject& remote, const QString& role);

} // namespace harmony::app
