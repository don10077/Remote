#pragma once

/**
 * @file labels.h
 * @brief Понятные пользователю названия для служебных значений конфигурации.
 *
 * В файле конфигурации типы, роли и действия хранятся как английские идентификаторы
 * («Television», «VOLUME», «92555930_PowerToggle_Hold»). Здесь они переводятся
 * в русские подписи для интерфейса. Неизвестные значения возвращаются как есть.
 */

#include "harmony/core/model/remoteConfig.h"

#include "harmony/core/error.h"
#include "harmony/core/ir/irSignal.h"

#include <QKeySequence>
#include <QString>

#include <string>
#include <vector>

namespace harmony::app::labels {

/// Поля распознанной ИК-команды для обучения и ручного ввода.
QString signalDetails(const ir::IrSignal& signal);

/// @return Русское название типа устройства («Television» → «Телевизор»).
QString deviceType(const std::string& type);

/// @return Типы устройств для выбора (ключи из файла пульта) в порядке показа.
const std::vector<std::string>& deviceTypes();

/// @return Русское название типа активности («VirtualDvd» → «Фильм»).
QString activityType(const std::string& type);

/// @return Русское название роли устройства в активности («VOLUME» → «Громкость»).
QString role(const std::string& name);

/**
 * @brief Описание действия кнопки.
 * @param config Конфигурация — чтобы найти устройство по id.
 * @param actionId Действие из файла: «<id устройства>_<команда>_<модификатор>».
 * @return Например «Hisense 65U7HQ · PowerToggle · удержание».
 */
QString action(const model::RemoteConfig& config, const std::string& actionId);

/// @return Русское название группы кнопок («Misc» → «Экран», «HardButtons» → «Клавиши»).
QString controlGroup(const std::string& name);

/**
 * @brief Подпись кнопки для таблицы.
 * @return Текст кнопки; если он пустой — имя значка в квадратных скобках;
 *         если нет и значка (аппаратная клавиша) — имя команды из действия.
 */
QString button(const model::Button& button);

/**
 * @brief Число с правильной формой слова: «1 device», «5 devices»; по-русски — 1 устройство, 2 устройства, 5 устройств.
 *
 * Перевод ищется по ключу «device|devices»; русский перевод даёт три формы через «|».
 * @param count Количество.
 * @param one Форма для одного («device»).
 * @param many Форма для остальных («devices»).
 */
QString plural(qsizetype count, const char* one, const char* many);

/// @return Название ИК-протокола на языке интерфейса («NEC (extended)» → «NEC (расширенный)»).
QString protocol(const std::string& name);

/// @return Краткое описание сигнала: «NEC · адрес 0x04, команда 0x08» или «протокол не распознан · …».
QString signalSummary(const ir::IrSignal& signal);

/**
 * @brief Текст ошибки ядра на языке интерфейса.
 *
 * Шаблон ошибки (Error::format) переводится, аргументы подставляются как есть,
 * причина (Error::cause) добавляется через «: ».
 */
QString errorText(const Error& error);

/// @return Сочетание клавиш стандартного действия, как его пишет система («Ctrl+Z», на macOS «⌘Z»).
QString shortcut(QKeySequence::StandardKey key);

} // namespace harmony::app::labels
