#pragma once

/**
 * @file propertyCatalog.h
 * @brief Свойства <Property> устройств и активностей: что они значат и какие бывают.
 *
 * В конфигурации их около сорока: так устройство говорит, что оно такое (вход
 * тюнера, сколько дисков), а активность — как себя вести (с какой страницы
 * начать, выключать ли ненужные устройства). Пульт меняет поведение по ним.
 *
 * Правило: сохраняется всё, описано то, что известно; неизвестное свойство
 * показывается под своим именем и правится как текст — не пропадает.
 *
 * Регистр значений важен: у устройств пишут true/false, у активностей —
 * True/False; это разные строки для пульта, поэтому формат значения задаёт
 * каталог (formatValue()).
 *
 * Описания — по каталогу проекта afterglow (GPL-3.0, library/properties.json),
 * собранному по настоящим конфигурациям Harmony 900.
 */

#include <span>
#include <string>
#include <string_view>

namespace harmony {

/// Где лежит свойство.
enum class PropertyScope {
    Device,       ///< <Device><Properties>
    DeviceTiming, ///< <Device><Commands><Properties>: паузы между нажатиями
    Activity,     ///< <Activity><Properties>
};

/// Описание известного свойства.
struct PropertyInfo {
    PropertyScope scope{};
    std::string_view name{};        ///< Как в файле: «PowerOffUnusedDevices»
    std::string_view label{};       ///< Подпись (по-английски)
    std::string_view description{}; ///< Что делает (по-английски)
    enum class Type { Bool, Int, Enum, Text } type = Type::Text;
    std::span<const std::string_view> values{}; ///< Для Enum — встречавшиеся значения
    int min = 0;                    ///< Для Int
    int max = 0;
    bool readOnly = false;          ///< Пишет сам пульт (IsNewDevice) — не править
};

/// @return Все известные свойства (по областям, в порядке показа).
[[nodiscard]] std::span<const PropertyInfo> propertyCatalog();

/// @return Описание свойства или nullptr — неизвестное.
[[nodiscard]] const PropertyInfo* findProperty(PropertyScope scope, std::string_view name);

/**
 * @brief Значение в том виде, в каком его пишет пульт.
 *
 * Логические: у устройств «true»/«false», у активностей «True»/«False».
 * @param value Значение (для логических — любое написание true/false/1/0).
 */
[[nodiscard]] std::string formatPropertyValue(PropertyScope scope, std::string_view name, std::string_view value);

} // namespace harmony
