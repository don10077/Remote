#pragma once

/**
 * @file remotePreferences.h
 * @brief Настройки самого пульта: звук клавиш, подсветка, шрифт, тема, часы, блокировка.
 *
 * Каждая настройка — отдельный текстовый файл platformconfig/system_<имя>.dat
 * (одна строка, без перевода строки). Пульт читает их при загрузке через свой
 * локальный адрес /system/<имя>; тот же адрес отвечает по USB-сети на порту 80,
 * поэтому настройку можно поменять и прямо на подключённом пульте.
 *
 * Формат часов хранится дважды: в system_timeformat.dat и в свойстве
 * TimeDisplayFormat пользователя в UserConfiguration.xml — при правке меняются оба
 * (иначе конфигурация противоречит сама себе, и пульт берёт значение из файла).
 *
 * Значения и границы — из прошивки (SysService.lua, GeneralOptions.as) по данным
 * проекта afterglow (GPL-3.0) и harmony900tools.
 */

#include "harmony/core/error.h"

#include <span>
#include <string>
#include <string_view>

namespace harmony {

/// Вариант значения настройки-выбора.
struct PreferenceChoice {
    std::string_view label; ///< Подпись (по-английски, переводится в интерфейсе)
    std::string_view value; ///< Что записывается в файл
};

/// Описание одной настройки пульта.
struct RemotePreference {
    std::string_view key{};          ///< Ключ: «sound», «backlight_level», ...
    std::string_view file{};         ///< Имя файла в platformconfig: «system_sound.dat»
    std::string_view label{};        ///< Подпись (по-английски)
    std::span<const PreferenceChoice> choices{}; ///< Варианты; пусто — число в [min, max]
    int min = 0;
    int max = 0;
    std::string_view defaultValue{}; ///< Значение прошивки по умолчанию
    std::string_view xmlProperty{};  ///< Свойство пользователя в UserConfiguration.xml; пусто — нет

    /// @return Адрес на пульте: «/system/sound».
    [[nodiscard]] std::string endpoint() const;

    /// @return Путь в архиве .EZHex: «platformconfig/system_sound.dat».
    [[nodiscard]] std::string archivePath() const;
};

/// @return Все известные настройки в порядке показа.
[[nodiscard]] std::span<const RemotePreference> remotePreferences();

/// @return Настройка по ключу или nullptr.
[[nodiscard]] const RemotePreference* findRemotePreference(std::string_view key);

/**
 * @brief Проверяет значение настройки.
 * @return ErrorCode::Format — значение не из списка или вне границ.
 */
[[nodiscard]] Result<void> checkPreferenceValue(const RemotePreference& preference, std::string_view value);

} // namespace harmony
