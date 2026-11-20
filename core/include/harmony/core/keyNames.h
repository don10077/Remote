#pragma once

/**
 * @file keyNames.h
 * @brief Имена физических клавиш Harmony: скан-код → имя, один общий список для всех семейств.
 *
 * В бинарных образах (архитектуры 8, 9, 10, 12, 14) записан только скан-код клавиши
 * (`key.tag & 63`) и привязка к команде. Имён клавиш в файле нет: таблица имён в секции 0
 * хранит имена состояний (`TV_Panasonic_Power_2`), а подписи есть только у экранных кнопок.
 * Значит имя физической клавиши — знание программы, и оно не должно зависеть от того, есть
 * ли у программы профиль модели или фото пульта.
 *
 * Список снят с конфигураций 650 (архитектура 14), где имена совпадают с зонами remote.json,
 * и подтверждён форматом 900: в `UserConfiguration.xml` 41 имя кнопок `HardButtons`, и оно
 * то же самое. Коды клавиш между семействами общие — таблица 880 (53 кода) делит 41 код с
 * 525 в том же порядке (dumps/EZHex/arch8/README.md, «Key codes look to be shared across
 * models»), H785 — 45 из 53. Поэтому имена применимы ко всем архитектурам, а не только к 14.
 *
 * Таблица плотная: `keyName()` даёт имя каждому коду от 0x01 до 0x3F, то есть всему диапазону
 * `key.tag & 63`. Подтверждённые данными имена (50) остаются как были; для кодов, имени
 * которых нет (`0x02`, `0x08`, `0x09`, `0x22`, `0x37…0x3F` — вероятнее всего мягкие клавиши
 * конкретных клавиатур), имя — `UnknownKey<код>`, например `UnknownKey09`. Такое имя ничего
 * не утверждает о клавише, но оно уникально, поэтому годится на `id` зоны, и его не надо
 * придумывать. Есть `keyNameConfirmed()`, по которой импортёр отличает настоящее имя от
 * заполнителя. Код `0x00` — не клавиша (пустое назначение), имени у него нет.
 */

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace harmony {

/// Префикс имени для кода, которого в подтверждённой части списка нет: UnknownKey09.
inline constexpr std::string_view kUnknownKeyPrefix = "UnknownKey";

struct KeyName {
    unsigned scan;         ///< Скан-код клавиши (`key.tag & 63`)
    std::string_view name; ///< Имя, которое показывает программа и пишет в `Button name`
};

/// Список по возрастанию кода от 0x01 до 0x3F: по этому порядку строятся списки клавиш.
[[nodiscard]] std::span<const KeyName> keyNames();

/**
 * @brief Имя клавиши по скан-коду, независимо от архитектуры и профиля.
 * @param scan Скан-код (`key.tag & 63`).
 * @return Имя из списка; nullopt — кода `0x00` и кодов выше 0x3F.
 */
[[nodiscard]] std::optional<std::string_view> keyName(unsigned scan);

/// Имя из списка или «Scan<N>», если код вышел за плотную часть таблицы.
[[nodiscard]] std::string keyLabel(unsigned scan);

/**
 * @brief Подтверждено ли имя данными, а не является заполнителем UnknownKey<N>.
 * @param scan Скан-код (`key.tag & 63`).
 * @return true — имя подтверждено (650 и XML 900); false — заполнитель или кода нет.
 */
[[nodiscard]] bool keyNameConfirmed(unsigned scan);

/// Имя — заполнитель UnknownKey<N>.
[[nodiscard]] bool isUnknownKeyName(std::string_view name);

/// Есть ли такое имя в списке (для проверки `id` зоны в редакторе).
[[nodiscard]] bool isKnownKeyName(std::string_view name);

} // namespace harmony
