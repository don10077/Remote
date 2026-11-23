#pragma once

#include "harmony/core/error.h"
#include "harmony/core/importReport.h"
#include "harmony/core/model/remoteConfig.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace harmony::formats {

/**
 * @brief Итог импорта HID-образа: модель и предупреждения отдельно.
 */
struct ImportedModel {
    model::RemoteConfig model;
    harmony::ImportReport report;
};

/**
 * @brief Импортирует устройства, активности, команды и назначения из бинарного HID-образа.
 * @param payload Образ без XML-заголовка EZHex; рамка и сумма проверяются при импорте.
 * @param modelId Id модели из identity EZHex (по skin в каталоге); пусто — модель неизвестна.
 * @return Модель и отчёт импорта либо ошибка формата. Семейство берётся из рамки образа,
 *         modelId уточняет скан-таблицу; без модели используется только таблица семейства.
 * Предупреждения импорта не сохраняются как свойства конфигурации.
 * Программы конфигурации не исполняются.
 */
[[nodiscard]] Result<ImportedModel> importBinaryHid(std::span<const std::uint8_t> payload,
                                                     std::string_view modelId = {});
} // namespace harmony::formats
