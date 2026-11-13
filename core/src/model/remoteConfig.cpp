/**
 * @file remoteConfig.cpp
 * @brief Поиск объектов в модели конфигурации пульта.
 */

#include "harmony/core/model/remoteConfig.h"

#include <algorithm>
#include <numeric>

namespace harmony::model {

const Command* Device::findCommand(const std::string& commandName) const {
    const auto it = std::ranges::find(commands, commandName, &Command::name);
    return it != commands.end() ? &*it : nullptr;
}

std::string Device::displayName() const {
    if (!label.empty()) {
        return label;
    }
    if (manufacturer.empty() || model.empty()) {
        return manufacturer + model; // одно из полей пустое — пробел не нужен
    }
    return manufacturer + " " + model;
}

bool Activity::usesDevice(const std::string& deviceId) const {
    return std::ranges::find(powerOn, deviceId) != powerOn.end() ||
           std::ranges::find(roles, deviceId, &ActivityRole::deviceId) != roles.end();
}

std::string RemoteConfig::property(const std::string& name) const {
    const auto it = std::ranges::find(properties, name, &Property::first);
    return it != properties.end() ? it->second : std::string{};
}

Device* RemoteConfig::findDevice(const std::string& deviceId) {
    const auto it = std::ranges::find(devices, deviceId, &Device::id);
    return it != devices.end() ? &*it : nullptr;
}

const Device* RemoteConfig::findDevice(const std::string& deviceId) const {
    const auto it = std::ranges::find(devices, deviceId, &Device::id);
    return it != devices.end() ? &*it : nullptr;
}

const Activity* RemoteConfig::findActivity(const std::string& activityId) const {
    const auto it = std::ranges::find(activities, activityId, &Activity::id);
    return it != activities.end() ? &*it : nullptr;
}

std::string Activity::roleDevice(const std::string& roleName) const {
    const auto found = std::ranges::find(roles, roleName, &ActivityRole::name);
    return found != roles.end() ? found->deviceId : std::string();
}

bool RemoteConfig::isDeviceUsed(const std::string& deviceId) const {
    return std::ranges::any_of(activities, [&deviceId](const Activity& activity) {
        return activity.usesDevice(deviceId);
    });
}

std::size_t RemoteConfig::userActivityCount() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(activities, [](const Activity& activity) { return !activity.isPowerOff(); }));
}

bool RemoteConfig::isActivityNotSent(const Activity& activity) const {
    if (activity.isPowerOff()) {
        return false;
    }
    if (activity.notSent) {
        return true;
    }
    return std::ranges::any_of(devices, [&activity](const Device& device) {
        return device.notSent && activityNeedsDevice(activity, device.id);
    });
}

bool RemoteConfig::activityNeedsDevice(const Activity& activity, const std::string& deviceId) {
    if (activity.usesDevice(deviceId)) {
        return true;
    }
    const std::string prefix = deviceId + "_"; // ActionId: <устройство>_<команда>_<модификатор>
    return std::ranges::any_of(activity.controlGroups, [&prefix](const ControlGroup& group) {
        return std::ranges::any_of(group.buttons,
                                   [&prefix](const Button& button) { return button.actionId.starts_with(prefix); });
    });
}

std::vector<const Device*> RemoteConfig::notSentDevicesOf(const Activity& activity) const {
    std::vector<const Device*> found;
    if (activity.isPowerOff()) {
        return found;
    }
    for (const auto& device : devices) {
        if (device.notSent && activityNeedsDevice(activity, device.id)) {
            found.push_back(&device);
        }
    }
    return found;
}

std::size_t RemoteConfig::commandCount() const {
    return std::accumulate(devices.begin(), devices.end(), std::size_t{0},
                           [](std::size_t sum, const Device& device) {
                               return sum + device.commands.size();
                           });
}

} // namespace harmony::model
