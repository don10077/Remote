#pragma once

/**
 * @file buttonIcons.h
 * @brief Значки экранных кнопок Harmony 900: какие имена знает прошивка.
 *
 * Экранная кнопка показывает либо подпись, либо значок:
 * @code
 *   <Button><Label></Label><Icon>aspect</Icon><Position>1</Position><ActionId>…</ActionId></Button>
 * @endcode
 * <Icon> — имя картинки во встроенной графике пульта (app-main.swf). Имени, которого
 * там нет, пульт не нарисует — кнопка останется пустой.
 *
 * Список имён — по выгрузке значков прошивки 900 из проекта afterglow
 * (icons/buttons, сами картинки — Logitech, сюда не копируются). Регистр важен.
 */

#include <span>
#include <string_view>

namespace harmony {

/// @return Имена значков, которые рисует Harmony 900, по алфавиту (без учёта регистра).
[[nodiscard]] std::span<const std::string_view> buttonIcons();

/**
 * @brief Есть ли значок в прошивке пульта.
 * @param name Имя значка, как в <Icon>.
 * @return true, если такой значок есть (с учётом регистра).
 */
[[nodiscard]] bool isKnownButtonIcon(std::string_view name);

} // namespace harmony
