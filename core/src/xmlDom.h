#pragma once

/**
 * @file xmlDom.h
 * @brief Внутренние помощники ядра для правки XML пульта без потерь (pugixml).
 *
 * Не публичный заголовок: pugixml не выходит за пределы ядра.
 */

#include "harmony/core/error.h"

#include <pugixml.hpp>

#include <string>
#include <string_view>

namespace harmony::detail {

/// Объявление XML — такое же, как в файлах с пульта.
inline constexpr std::string_view kDeclaration = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

/**
 * @brief Метка пустого текста: символ U+E000 из частной области Юникода.
 *
 * Пустые текстовые поля пульт пишет парой тегов (<Label></Label>). Пустой
 * текстовый узел pugixml в разных версиях записывает по-разному (1.15 — парой,
 * 1.16 — <Label/>), поэтому такой элемент получает текст-метку, а saveDom()
 * вырезает её из результата. В файлах пульта этот символ не встречается.
 */
inline constexpr std::string_view kEmptyText = "\xEE\x80\x80";

/// Текст элемента; метка пустого текста — пустая строка.
[[nodiscard]] std::string_view textOf(pugi::xml_node node);

/**
 * @brief Загружает XML в дерево так, чтобы обратная запись совпала с исходником байт в байт.
 *
 * Пульт пишет пустые элементы двумя способами: контейнеры — «<Properties/>»,
 * текстовые поля — «<Label></Label>». В дереве pugixml пустой текст пропадает,
 * поэтому элементам, записанным парой тегов, возвращается пустой текстовый узел.
 */
[[nodiscard]] Result<void> loadDom(pugi::xml_document& document, const std::string& xml);

/// Записывает дерево в текст в формате пульта (объявление, затем XML одной строкой; метки пустого текста убираются).
[[nodiscard]] std::string saveDom(const pugi::xml_document& document);

/// Элемент верхнего уровня (<Device>, <Activity>) по тексту дочернего <Id>.
[[nodiscard]] pugi::xml_node findById(pugi::xml_node root, const char* tag, const std::string& id);

/// Задаёт подпись <Presentation><Label>, создавая элементы при необходимости.
void setLabel(pugi::xml_node owner, const std::string& label);

/**
 * @brief Добавляет текстовый элемент; пустой пишется парой тегов, как у пульта: <Label></Label>
 *        (через метку kEmptyText).
 * @return Добавленный элемент.
 */
pugi::xml_node appendText(pugi::xml_node parent, const char* name, std::string_view text);

/// Добавляет <Property name="…">значение</Property>.
void appendProperty(pugi::xml_node properties, const char* name, std::string_view value);

/// Задаёт значение <Property name="…"> (добавляет свойство, если его нет).
void setProperty(pugi::xml_node properties, const char* name, std::string_view value);

/**
 * @brief Добавляет действие SetValue: перевести состояние устройства в значение.
 *
 * Так пульт выключает устройство в «выключить всё» (State=Power, Value=Off)
 * и переключает вход при запуске активности (State=Input).
 */
void appendSetValue(pugi::xml_node actions, const std::string& deviceId, std::string_view state,
                    std::string_view value);

} // namespace harmony::detail
