#pragma once

/**
 * @file activityDraft.h
 * @brief Новая активность: что задаёт пользователь и как по этому назначаются кнопки пульта.
 *
 * Пользователь выбирает устройства и их роли: кто управляет (меню, стрелки,
 * воспроизведение, цифры), кто отвечает за громкость, что показывает картинку,
 * и какой вход переключить. Кнопки пульта назначаются сами: каждой кнопке
 * ищется команда с подходящим именем у нужного устройства («Громкость +» →
 * VolumeUp у устройства громкости и т. д.).
 */

#include "harmony/core/model/remoteConfig.h"

#include <string>
#include <utility>
#include <vector>

namespace harmony {

/// Данные новой активности.
struct ActivityDraft {
    std::string label;                      ///< Имя на экране пульта
    std::string type = "VirtualTelevisionN"; ///< Тип (см. activityTypes())
    std::vector<std::string> devices;       ///< Устройства, которые включаются
    std::string controlDevice;              ///< Управление (роль DEFAULT); обязательно, из devices
    std::string volumeDevice;               ///< Громкость (VOLUME); пусто — как у управления
    std::string displayDevice;              ///< Изображение (DISPLAY); пусто — нет
    /// Входы, которые переключить при запуске: устройство → вход (из Device::inputs).
    std::vector<std::pair<std::string, std::string>> inputs;
};

/// Назначение одной аппаратной кнопки.
struct HardButtonAssignment {
    std::string key;      ///< Кнопка пульта («VolumeUp», «Number1», ...)
    std::string deviceId; ///< Устройство, которому уходит команда
    std::string command;  ///< Имя команды устройства
};

/**
 * @brief Черновик с текущим составом активности (для правки).
 * @param activity Активность.
 */
[[nodiscard]] ActivityDraft draftOf(const model::Activity& activity);

/**
 * @brief Сколько кнопок активности шлют команды устройствам не из черновика.
 *
 * Именно столько кнопок удалит updateActivity() — их стоит показать заранее.
 */
[[nodiscard]] int buttonsLostBy(const model::Activity& activity, const ActivityDraft& draft);

/// @return Поддерживаемые типы активностей в порядке показа (первый — по умолчанию).
[[nodiscard]] const std::vector<std::string>& activityTypes();

/**
 * @brief Назначает аппаратные кнопки пульта для черновика.
 *
 * Громкость и «без звука» — устройству громкости (или управления), остальные
 * кнопки — устройству управления. Кнопка без подходящей команды не назначается.
 * @param config Конфигурация (команды устройств).
 * @param draft Черновик.
 * @return Назначения в порядке кнопок на пульте.
 */
[[nodiscard]] std::vector<HardButtonAssignment> planHardButtons(const model::RemoteConfig& config,
                                                                const ActivityDraft& draft);

} // namespace harmony
