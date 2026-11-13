#pragma once

/**
 * @file configDiff.h
 * @brief Что изменится на пульте: сравнение конфигурации на пульте с той, что будет записана.
 *
 * Устройства и активности сопоставляются по id (запись их сохраняет).
 * У устройств сравниваются имя, тип, модель, команды (по сигналу), кнопки,
 * питание и входы; у активностей — имя, тип, место в списке, включаемые
 * устройства, роли, входы, кнопки.
 */

#include "harmony/core/configCheck.h"

#include <string>
#include <vector>

namespace harmony {

class ConfigDocument;

/// Одно отличие для показа пользователю.
struct ConfigChange {
    enum class Kind {
        Added,   ///< Появится на пульте
        Removed, ///< Пропадёт с пульта
        Changed, ///< Изменится
    };
    enum class Area {
        Device,
        Activity,
    };
    Kind kind = Kind::Changed;
    Area area = Area::Device;
    std::string item; ///< Имя устройства или активности (новое)
    std::vector<Message> details; ///< Что именно изменилось
};

/**
 * @brief Сравнивает две конфигурации.
 * @param before Что на пульте сейчас.
 * @param after Что будет записано.
 * @return Отличия: сначала устройства, затем активности; пусто — одинаковы.
 */
[[nodiscard]] std::vector<ConfigChange> compareConfigurations(const ConfigDocument& before,
                                                              const ConfigDocument& after);

} // namespace harmony
