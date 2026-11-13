/**
 * @file configDocumentReplace.cpp
 * @brief Замена устройства в активностях и подбор команд нового устройства.
 */

#include "configDocumentImpl.h"
#include "harmony/core/deviceReplacement.h"
#include "harmony/core/macro.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace harmony {

namespace {

using detail::textOf;

std::string parameter(pugi::xml_node operation, const char* name) {
    return std::string(textOf(operation.find_child_by_attribute("Parameter", "name", name)));
}

void setParameter(pugi::xml_node operation, const char* name, const std::string& value) {
    operation.find_child_by_attribute("Parameter", "name", name).text() = value.c_str();
}

/// Имя без регистра, пробелов, «_» и «-»: «Volume_Up» = «volume up» = «VolumeUp».
std::string normalized(const std::string& name) {
    std::string out;
    for (const unsigned char c : name) {
        if (c != ' ' && c != '_' && c != '-') {
            out += static_cast<char>(std::tolower(c));
        }
    }
    return out;
}

/// Привычные синонимы — к одному имени: «Mute» = «VolumeMute», «1» = «Number1»…
std::string canonical(const std::string& name) {
    std::string n = normalized(name);
    for (const char* prefix : {"number", "digit", "num"}) {
        if (n.starts_with(prefix) && n.size() == std::string_view(prefix).size() + 1 &&
            std::isdigit(static_cast<unsigned char>(n.back())) != 0) {
            return n.substr(n.size() - 1);
        }
    }
    static const std::map<std::string, std::string> aliases = {
        {"volumemute", "mute"},       {"mutetoggle", "mute"},       {"power", "powertoggle"},
        {"standby", "powertoggle"},   {"volup", "volumeup"},        {"voldown", "volumedown"},
        {"volumeplus", "volumeup"},   {"volumeminus", "volumedown"}, {"chup", "channelup"},
        {"chdown", "channeldown"},    {"channelplus", "channelup"}, {"channelminus", "channeldown"},
        {"ok", "select"},             {"enter", "select"},          {"up", "directionup"},
        {"down", "directiondown"},    {"left", "directionleft"},    {"right", "directionright"},
        {"return", "back"},           {"prev", "skipback"},         {"next", "skipforward"},
        {"ff", "fastforward"},        {"rew", "rewind"},            {"source", "inputnext"},
        {"input", "inputnext"},       {"epg", "guide"},             {"exit", "exit"},
    };
    const auto it = aliases.find(n);
    return it != aliases.end() ? it->second : n;
}

/// Действие кнопки «<устройство>_<команда>_<модификатор>» по частям.
struct ActionParts {
    std::string device;
    std::string command;
    std::string modifier;
};

ActionParts split(const std::string& actionId) {
    const auto first = actionId.find('_');
    const auto last = actionId.rfind('_');
    if (first == std::string::npos || last <= first) {
        return {actionId, {}, {}};
    }
    return {actionId.substr(0, first), actionId.substr(first + 1, last - first - 1), actionId.substr(last + 1)};
}

} // namespace

std::vector<std::pair<std::string, int>> ConfigDocument::usedDeviceCommands(
    const std::string& deviceId, const std::vector<std::string>& activities) const {
    std::map<std::string, int> used;
    pugi::xml_document dom;
    if (!detail::loadDom(dom, impl_->xml)) {
        return {};
    }
    for (const auto& activity : impl_->model.activities) {
        if (activity.isPowerOff() ||
            (!activities.empty() && std::ranges::find(activities, activity.id) == activities.end())) {
            continue;
        }
        for (const auto& group : activity.controlGroups) {
            for (const auto& button : group.buttons) {
                if (isMacroAction(button.actionId)) {
                    // Команды шагов макроса тоже нужно сопоставить при замене.
                    if (auto steps = macro(button.actionId)) {
                        for (const MacroStep& item : *steps) {
                            if (item.deviceId == deviceId && item.step.kind == StateStep::Kind::Command) {
                                ++used[item.step.command];
                            }
                        }
                    }
                    continue;
                }
                const ActionParts parts = split(button.actionId);
                if (parts.device == deviceId && !parts.command.empty()) {
                    ++used[parts.command];
                }
            }
        }
        const pugi::xml_node node = detail::findById(dom.child("Root"), "Activity", activity.id);
        for (const pugi::xml_node action : node.child("EnterActions").children("Action")) {
            const pugi::xml_node operation = action.child("Operation");
            if (textOf(operation.child("Name")) == "SendCommand" && parameter(operation, "DeviceId") == deviceId) {
                ++used[parameter(operation, "Command")];
            }
        }
    }
    return {used.begin(), used.end()};
}

std::string ConfigDocument::matchDeviceCommand(const std::string& newId, const std::string& command) const {
    const model::Device* device = impl_->model.findDevice(newId);
    if (device == nullptr) {
        return {};
    }
    if (device->findCommand(command) != nullptr) {
        return command;
    }
    for (const auto& own : device->commands) {
        if (normalized(own.name) == normalized(command)) {
            return own.name;
        }
    }
    for (const auto& own : device->commands) {
        if (canonical(own.name) == canonical(command)) {
            return own.name;
        }
    }
    return {};
}

Result<DeviceReplacementReport> ConfigDocument::replaceDevice(const DeviceReplacement& replacement) {
    const model::RemoteConfig& config = impl_->model;
    const model::Device* oldDevice = config.findDevice(replacement.oldId);
    const model::Device* newDevice = config.findDevice(replacement.newId);
    if (oldDevice == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {replacement.oldId});
    }
    if (newDevice == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {replacement.newId});
    }
    if (replacement.oldId == replacement.newId) {
        return makeError(ErrorCode::Format, "the device cannot replace itself");
    }
    // Таблица команд: заданное пользователем, остальное — подбором.
    auto mapped = [&](const std::string& command) {
        if (const auto it = replacement.commands.find(command); it != replacement.commands.end()) {
            return newDevice->findCommand(it->second) != nullptr ? it->second : std::string();
        }
        return matchDeviceCommand(replacement.newId, command);
    };
    const std::vector<std::string> newInputs = newDevice->inputs;
    const std::string oldId = replacement.oldId;
    const std::string newId = replacement.newId;

    const auto included = [&replacement](const std::string& activityId) {
        return replacement.activities.empty() ||
               std::ranges::find(replacement.activities, activityId) != replacement.activities.end();
    };

    DeviceReplacementReport report;

    // Макросы выбранных активностей: шаги живут в ActionLists.xml, поэтому правятся
    // до UserConfiguration.xml и применяются только вместе с ним (как в setMacro).
    std::set<std::string> macros;
    for (const auto& activity : config.activities) {
        if (activity.isPowerOff() || !included(activity.id)) {
            continue;
        }
        for (const auto& group : activity.controlGroups) {
            for (const auto& button : group.buttons) {
                if (isMacroAction(button.actionId)) {
                    macros.insert(button.actionId);
                }
            }
        }
    }
    std::string updatedLists;      ///< Новый ActionLists.xml; пусто — макросы не менялись
    std::set<std::string> emptied; ///< Макросы, у которых не осталось шагов: их кнопки убираются
    if (!macros.empty()) {
        auto files = detail::currentFiles(*impl_);
        if (!files) {
            return std::unexpected(std::move(files.error()));
        }
        pugi::xml_document lists;
        if (auto loaded = detail::loadDom(lists, files->actionLists); !loaded) {
            return std::unexpected(std::move(loaded.error()));
        }
        std::set<std::string> changedMacros;
        for (const std::string& name : macros) {
            pugi::xml_node list = lists.child("Root").find_child_by_attribute("ActionList", "name", name.c_str());
            if (!list) {
                continue;
            }
            for (pugi::xml_node action = list.child("Action"); action;) {
                const pugi::xml_node next = action.next_sibling("Action");
                pugi::xml_node operation = action.child("Operation");
                if (parameter(operation, "DeviceId") != oldId) {
                    action = next;
                    continue;
                }
                changedMacros.insert(name);
                const std::string_view kind = textOf(operation.child("Name"));
                bool keep = true;
                if (kind == "SendCommand") {
                    const std::string command = mapped(parameter(operation, "Command"));
                    keep = !command.empty();
                    if (keep) {
                        setParameter(operation, "Command", command);
                        ++report.macroSteps;
                    } else {
                        ++report.removed;
                    }
                } else if (kind == "SetValue" && parameter(operation, "State") == "Input") {
                    keep = std::ranges::find(newInputs, parameter(operation, "Value")) != newInputs.end();
                    if (!keep) {
                        ++report.inputsLost;
                    }
                } else if (kind == "SetValue" && parameter(operation, "State") == "Power") {
                    keep = newDevice->powerControlled;
                }
                // Паузы (SendDelay) и прочее переходят как есть.
                if (keep) {
                    setParameter(operation, "DeviceId", newId);
                } else {
                    list.remove_child(action);
                }
                action = next;
            }
            if (!list.child("Action")) {
                emptied.insert(name);
            }
        }
        if (!changedMacros.empty()) {
            updatedLists = detail::saveDom(lists);
        }
        macros = std::move(changedMacros); // дальше нужны только изменённые
    }

    auto changed = detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        for (pugi::xml_node activity : root.children("Activity")) {
            const std::string id(textOf(activity.child("Id")));
            if (textOf(activity.child("Type")) == "PowerOff" || !included(id)) {
                continue;
            }
            bool touched = false;

            // Питание: новое включается, если включалось прежнее (или уже само).
            pugi::xml_node power = activity.child("Power");
            bool oldOn = false;
            bool newOn = false;
            bool listed = false;
            for (pugi::xml_node node = power.first_child(); node;) {
                const pugi::xml_node next = node.next_sibling();
                const std::string device(textOf(node));
                const bool on = std::string_view(node.name()) == "On";
                if (device == oldId) {
                    oldOn = oldOn || on;
                    listed = true;
                    power.remove_child(node);
                } else if (device == newId) {
                    newOn = newOn || on;
                    power.remove_child(node);
                }
                node = next;
            }
            if (listed) {
                touched = true;
                // «Вкл» — перед всеми «Выкл», как пишет пульт.
                if (oldOn || newOn) {
                    pugi::xml_node firstOff = power.child("Off");
                    pugi::xml_node node = firstOff ? power.insert_child_before("On", firstOff) : power.append_child("On");
                    node.text() = newId.c_str();
                } else {
                    detail::appendText(power, "Off", newId);
                }
                // Прежнее — выключено (оно остаётся в конфигурации).
                detail::appendText(power, "Off", oldId);
            } else if (newOn) {
                pugi::xml_node firstOff = power.child("Off");
                (firstOff ? power.insert_child_before("On", firstOff) : power.append_child("On")).text() =
                    newId.c_str();
            }

            // Роли.
            for (pugi::xml_node role : activity.children("Role")) {
                if (textOf(role.child("DeviceId")) == oldId) {
                    role.child("DeviceId").text() = newId.c_str();
                    touched = true;
                }
            }

            // Шаги запуска: команды — сопоставленные, входы — если есть у нового.
            pugi::xml_node enter = activity.child("EnterActions");
            for (pugi::xml_node action = enter.child("Action"); action;) {
                const pugi::xml_node next = action.next_sibling("Action");
                pugi::xml_node operation = action.child("Operation");
                if (parameter(operation, "DeviceId") == oldId) {
                    touched = true;
                    const std::string_view kind = textOf(operation.child("Name"));
                    bool keep = true;
                    if (kind == "SendCommand") {
                        const std::string command = mapped(parameter(operation, "Command"));
                        keep = !command.empty();
                        if (keep) {
                            setParameter(operation, "Command", command);
                            ++report.steps;
                        } else {
                            ++report.removed;
                        }
                    } else if (kind == "SetValue" && parameter(operation, "State") == "Input") {
                        keep = std::ranges::find(newInputs, parameter(operation, "Value")) != newInputs.end();
                        if (!keep) {
                            ++report.inputsLost;
                        }
                    } else if (kind == "SetValue" && parameter(operation, "State") == "Power") {
                        keep = newDevice->powerControlled; // вручную — пульт его не включает
                    }
                    if (keep) {
                        setParameter(operation, "DeviceId", newId);
                    } else {
                        enter.remove_child(action);
                    }
                }
                action = next;
            }

            // Кнопки: аппаратные и экранные.
            for (pugi::xml_node group : activity.child("Presentation").children("ControlGroup")) {
                for (pugi::xml_node button = group.child("Button"); button;) {
                    const pugi::xml_node next = button.next_sibling("Button");
                    const std::string actionId(textOf(button.child("ActionId")));
                    const ActionParts parts = split(actionId);
                    if (emptied.contains(actionId)) {
                        // Макрос слал только команды, которых у нового нет.
                        touched = true;
                        group.remove_child(button);
                        ++report.removed;
                    } else if (macros.contains(actionId)) {
                        touched = true; // шаги поменялись в ActionLists.xml
                    } else if (parts.device == oldId) {
                        touched = true;
                        const std::string command = mapped(parts.command);
                        if (command.empty()) {
                            group.remove_child(button);
                            ++report.removed;
                        } else {
                            button.child("ActionId").text() =
                                (newId + "_" + command + "_" + (parts.modifier.empty() ? "Hold" : parts.modifier))
                                    .c_str();
                            ++report.buttons;
                        }
                    }
                    button = next;
                }
            }
            if (touched) {
                ++report.activities;
            }
        }
        return {};
    });
    if (!changed) {
        return std::unexpected(std::move(changed.error()));
    }
    if (!updatedLists.empty()) {
        impl_->actionLists = std::move(updatedLists);
    }
    if (replacement.removeOld && activitiesUsingDevice(oldId).empty()) {
        if (auto removed = removeDevice(oldId); removed) {
            report.oldRemoved = true;
        }
    }
    return report;
}

} // namespace harmony
