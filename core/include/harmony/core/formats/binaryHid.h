#pragma once

#include "harmony/core/error.h"
#include "harmony/core/model/remoteConfig.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace harmony::formats {
/**
 * @brief Импортирует устройства, активности, команды и назначения из бинарного HID-образа.
 * @param payload Образ без XML-заголовка EZHex; рамка и сумма проверяются при импорте.
 * @param modelId Id модели из identity EZHex (по skin в каталоге); пусто — модель неизвестна.
 * @return Модель редактора либо ошибка формата. Семейство берётся из рамки образа,
 *         modelId уточняет скан-таблицу; без модели используется только таблица семейства.
 * Программы конфигурации не исполняются.
 */
[[nodiscard]] Result<model::RemoteConfig> importBinaryHid(std::span<const std::uint8_t> payload,
                                                       std::string_view modelId = {});
}
