#pragma once

/**
 * @file nameCheck.h
 * @brief Занятые имена устройств и активностей.
 *
 * Пульт показывает только имена, поэтому одинаковые путают. Окна ввода проверяют имя
 * сразу; «Проверка» конфигурации находит повторы, которые уже есть в файле.
 */

#include "harmony/core/model/remoteConfig.h"

#include <QString>

#include <string>

namespace harmony::app {

/**
 * @brief Носит ли это имя другое устройство (без учёта регистра и пробелов по краям).
 * @param config Конфигурация.
 * @param name Проверяемое имя.
 * @param exceptId Устройство, которое переименовывают (его имя не считается); пусто — новое.
 */
[[nodiscard]] inline bool deviceNameTaken(const model::RemoteConfig& config, const QString& name,
                                          const std::string& exceptId = {}) {
    const QString wanted = name.trimmed();
    for (const auto& device : config.devices) {
        if (device.id != exceptId &&
            QString::fromStdString(device.displayName()).trimmed().compare(wanted, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Носит ли это имя другая активность; «Выключить всё» не в счёт (на экране её нет).
 * @param config Конфигурация.
 * @param name Проверяемое имя.
 * @param exceptId Активность, которую переименовывают; пусто — новая.
 */
[[nodiscard]] inline bool activityNameTaken(const model::RemoteConfig& config, const QString& name,
                                            const std::string& exceptId = {}) {
    const QString wanted = name.trimmed();
    for (const auto& activity : config.activities) {
        if (!activity.isPowerOff() && activity.id != exceptId &&
            QString::fromStdString(activity.label).trimmed().compare(wanted, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace harmony::app
