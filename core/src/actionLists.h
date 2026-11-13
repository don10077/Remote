#pragma once

/**
 * @file actionLists.h
 * @brief ActionLists.xml: что именно делает каждая кнопка пульта.
 *
 * Кнопка в UserConfiguration.xml ссылается на действие по имени (<ActionId>),
 * а само действие описано в ActionLists.xml:
 * @code
 * <ActionList name="92555930_Menu_Hold"><Action><Target>Device</Target><Operation>
 *   <Name>SendCommand</Name><Parameter name="DeviceId">92555930</Parameter>
 *   <Parameter name="Command">Menu</Parameter><Parameter name="Modifier">Hold</Parameter>
 * </Operation></Action></ActionList>
 * @endcode
 * Имя всегда «<устройство>_<команда>_<Hold|Press>», поэтому файл полностью
 * выводится из конфигурации: при сохранении в нём остаются действия всех
 * кнопок, лишние убираются, недостающие добавляются в конец. Если ничего не
 * менялось, файл остаётся байт в байт прежним.
 */

#include "harmony/core/error.h"
#include "harmony/core/model/remoteConfig.h"

#include <string>
#include <string_view>

namespace harmony::detail {

/// Имя файла действий в папке конфигурации.
inline constexpr std::string_view kActionListsFileName = "ActionLists.xml";

/**
 * @brief Приводит ActionLists.xml в соответствие с кнопками конфигурации.
 * @param existing Текущий текст файла; пусто — файла нет, он создаётся заново.
 * @param config Конфигурация: действия берутся из кнопок устройств и активностей.
 * @param protocolHash Хеш протоколов («0x…») для свойства и <Protocols><Hash>; пусто — не трогать.
 * @param lastUpdated Значение LastUpdated для нового файла.
 * @return Текст файла; ErrorCode::Format, если существующий файл не разбирается.
 */
[[nodiscard]] Result<std::string> syncActionLists(const std::string& existing,
                                                  const model::RemoteConfig& config,
                                                  const std::string& protocolHash,
                                                  const std::string& lastUpdated);

} // namespace harmony::detail
