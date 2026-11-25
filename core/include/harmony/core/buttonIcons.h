#pragma once

/**
 * @file buttonIcons.h
 * @brief Значки экранных кнопок: имена, которые знает семейство модели (U6).
 *
 * Экранная кнопка показывает либо подпись, либо значок:
 * @code
 *   <Button><Label></Label><Icon>aspect</Icon><Position>1</Position><ActionId>…</ActionId></Button>
 * @endcode
 * <Icon> — имя картинки во встроенной графике пульта (app-main.swf). Имени, которого
 * там нет, пульт не нарисует — кнопка останется пустой.
 *
 * Список имён — данные семейства (`RemoteFamily::buttonIcons`), а не один общий список:
 * семейство без имён ничего не утверждает. Источник arch 15 — выгрузка значков прошивки 900.
 * Регистр важен.
 */

#include "harmony/core/modelInfo.h"

#include <span>
#include <string_view>

namespace harmony {

/**
 * @brief Имена значков, которые знает семейство модели.
 * @param model Модель документа. Пустой id — модель не выбрана: прежний общий список, который
 *        не проверен для этой модели.
 * @return Список по алфавиту (без учёта регистра). Пустой — имён значков у семейства нет,
 *         и проверке нечего утверждать.
 */
[[nodiscard]] std::span<const std::string_view> buttonIcons(const ModelInfo& model);

/**
 * @brief Есть ли значок в прошивке пульта.
 * @param model Модель документа.
 * @param name Имя значка, как в <Icon>.
 * @return true, если такой значок есть (с учётом регистра).
 */
[[nodiscard]] bool isKnownButtonIcon(const ModelInfo& model, std::string_view name);

} // namespace harmony
