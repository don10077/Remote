/**
 * @file configDocumentEdit.cpp
 * @brief Правка конфигурации: переименование, перестановка, удаление.
 *
 * Каждая правка меняет только нужные элементы XML; остальной файл остаётся
 * байт в байт прежним. При ошибке документ не меняется.
 */

#include "configDocumentImpl.h"
#include "harmony/core/activityDraft.h"
#include "harmony/core/remoteProfile.h"

#include <algorithm>
#include <vector>

namespace harmony {

using detail::findById;
using detail::mutate;

std::string ConfigDocument::targetRemote() const {
    const auto target = model().property("HarmonyTargetRemote");
    if (!target.empty()) return target;
    if (identity().architecture == 11 && identity().skin >= 62 && identity().skin <= 64) return "1100";
    return identity().architecture == 15 && identity().skin == 61 ? "900" : "unknown";
}

Result<void> ConfigDocument::setTargetRemote(const std::string& id) {
    if (!remoteProfile(id)) return makeError(ErrorCode::Unsupported, "unknown target remote: %1", {id});
    return mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        auto properties = root.child("Properties");
        if (!properties) properties = root.prepend_child("Properties");
        auto property = properties.find_child_by_attribute("Property", "name", "HarmonyTargetRemote");
        if (!property) {
            property = properties.append_child("Property");
            property.append_attribute("name") = "HarmonyTargetRemote";
        }
        property.text() = id.c_str();
        return {};
    });
}

Result<void> ConfigDocument::setDeviceLabel(const std::string& deviceId, const std::string& label) {
    if (label.empty()) {
        return makeError(ErrorCode::Format, "the device name cannot be empty");
    }
    return mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node device = findById(root, "Device", deviceId);
        if (!device) {
            return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
        }
        detail::setLabel(device, label);
        return {};
    });
}

Result<void> ConfigDocument::setActivityLabel(const std::string& activityId, const std::string& label) {
    if (label.empty()) {
        return makeError(ErrorCode::Format, "the activity name cannot be empty");
    }
    return mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node activity = findById(root, "Activity", activityId);
        if (!activity) {
            return makeError(ErrorCode::NotFound, "no activity %1", {activityId});
        }
        detail::setLabel(activity, label);
        return {};
    });
}

Result<void> ConfigDocument::moveActivity(const std::string& activityId, std::size_t index) {
    return mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node activity = findById(root, "Activity", activityId);
        if (!activity) {
            return makeError(ErrorCode::NotFound, "no activity %1", {activityId});
        }
        // Остальные активности по порядку — без переставляемой.
        std::vector<pugi::xml_node> others;
        for (pugi::xml_node node : root.children("Activity")) {
            if (node != activity) {
                others.push_back(node);
            }
        }
        if (others.empty()) {
            return {};
        }
        if (index < others.size()) {
            root.insert_move_before(activity, others[index]);
        } else {
            root.insert_move_after(activity, others.back());
        }
        return {};
    });
}

Result<void> ConfigDocument::moveDevice(const std::string& deviceId, std::size_t index) {
    return mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node device = findById(root, "Device", deviceId);
        if (!device) {
            return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
        }
        // Остальные устройства по порядку — без переставляемого.
        std::vector<pugi::xml_node> others;
        for (pugi::xml_node node : root.children("Device")) {
            if (node != device) {
                others.push_back(node);
            }
        }
        if (others.empty()) {
            return {};
        }
        if (index < others.size()) {
            root.insert_move_before(device, others[index]);
        } else {
            root.insert_move_after(device, others.back());
        }
        return {};
    });
}

Result<void> ConfigDocument::removeActivity(const std::string& activityId) {
    return mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node activity = findById(root, "Activity", activityId);
        if (!activity) {
            return makeError(ErrorCode::NotFound, "no activity %1", {activityId});
        }
        if (std::string_view(activity.child("Type").text().get()) == "PowerOff") {
            return makeError(ErrorCode::Unsupported,
                             "the service activity “turn everything off” cannot be deleted");
        }
        root.remove_child(activity);
        return {};
    });
}

std::vector<std::string> ConfigDocument::activitiesUsingDevice(const std::string& deviceId) const {
    std::vector<std::string> names;
    const std::string actionPrefix = deviceId + "_"; // ActionId: <устройство>_<команда>_<Press|Hold>
    for (const auto& activity : impl_->model.activities) {
        bool used = activity.usesDevice(deviceId);
        for (const auto& group : activity.controlGroups) {
            for (const auto& button : group.buttons) {
                used = used || button.actionId.starts_with(actionPrefix);
            }
        }
        if (used) {
            names.push_back(activity.label);
        }
    }
    return names;
}

Result<void> ConfigDocument::removeDevice(const std::string& deviceId) {
    if (const auto usedIn = activitiesUsingDevice(deviceId); !usedIn.empty()) {
        std::string list;
        for (const auto& name : usedIn) {
            list += (list.empty() ? "«" : ", «") + name + "»";
        }
        return makeError(ErrorCode::Unsupported, "the device is used in activities: %1", {list});
    }

    // Протоколы устройства остаются в IrProto.bin: другие устройства могут ссылаться на них же,
    // а лишняя запись пульту не мешает. Действия его кнопок уйдут из ActionLists.xml при сохранении.
    return mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node device = findById(root, "Device", deviceId);
        if (!device) {
            return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
        }
        for (pugi::xml_node activity : root.children("Activity")) {
            // Выключение устройства при запуске активности.
            pugi::xml_node power = activity.child("Power");
            for (pugi::xml_node off = power.child("Off"); off;) {
                pugi::xml_node next = off.next_sibling("Off");
                if (deviceId == off.text().get()) {
                    power.remove_child(off);
                }
                off = next;
            }
            // Действия при входе, адресованные устройству (например, «выключить» в PowerOff).
            pugi::xml_node enter = activity.child("EnterActions");
            for (pugi::xml_node action = enter.child("Action"); action;) {
                pugi::xml_node next = action.next_sibling("Action");
                const pugi::xml_node target = action.child("Operation").find_child_by_attribute(
                    "Parameter", "name", "DeviceId");
                if (target && deviceId == target.text().get()) {
                    enter.remove_child(action);
                }
                action = next;
            }
        }
        root.remove_child(device);
        return {};
    });
}

Result<std::vector<std::string>> ConfigDocument::removeDeviceFromActivities(const std::string& deviceId) {
    if (model().findDevice(deviceId) == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    // Копия: каждая правка перечитывает модель, ссылки на активности устаревают.
    const std::vector<model::Activity> activities = model().activities;
    const Snapshot before = snapshot();
    std::vector<std::string> removed;
    auto changed = [&]() -> Result<void> {
        for (const auto& activity : activities) {
            if (activity.isPowerOff() || !model::RemoteConfig::activityNeedsDevice(activity, deviceId)) {
                continue;
            }
            ActivityDraft draft = draftOf(activity);
            const bool turnedOn = std::erase(draft.devices, deviceId) > 0;
            if (turnedOn && draft.devices.empty()) {
                if (auto gone = removeActivity(activity.id); !gone) {
                    return gone;
                }
                removed.push_back(activity.label);
                continue;
            }
            for (std::string* role : {&draft.controlDevice, &draft.volumeDevice, &draft.displayDevice}) {
                if (*role == deviceId) {
                    role->clear(); // управление checkDraft отдаст первому устройству
                }
            }
            std::erase_if(draft.inputs, [&deviceId](const auto& input) { return input.first == deviceId; });
            if (auto updated = updateActivity(activity.id, draft, false); !updated) {
                return updated;
            }
        }
        return {};
    }();
    if (!changed) {
        // Активности меняются по одной — при ошибке на середине возвращаем все.
        if (auto restored = restore(before); !restored) {
            return std::unexpected(std::move(restored.error()));
        }
        return std::unexpected(std::move(changed.error()));
    }
    return removed;
}

} // namespace harmony
