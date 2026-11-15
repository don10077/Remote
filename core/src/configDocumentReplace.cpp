/**
 * @file configDocumentReplace.cpp
 * @brief Замена устройства в активностях и подбор команд нового устройства.
 */

#include "configDocumentImpl.h"
#include "harmony/core/deviceReplacement.h"
#include "harmony/core/macro.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

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

namespace {

/// Что нужно этапам замены: устройства, подбор команд и отчёт.
struct ReplaceContext {
    std::string oldId;
    std::string newId;
    std::vector<std::string> newInputs;           ///< Входы нового устройства
    bool newPowerControlled = false;              ///< Питание нового включает пульт
    std::function<std::string(const std::string&)> mapped; ///< Команда нового; пусто — убрать
    DeviceReplacementReport* report = nullptr;
};

/**
 * @brief Перевести шаг (макроса или запуска активности) прежнего устройства на новое.
 *
 * Команда — сопоставленная, вход — если есть у нового, питание — если его включает пульт;
 * паузы (SendDelay) и прочее переходят как есть.
 * @param operation <Operation> шага с DeviceId прежнего устройства.
 * @param moved Счётчик отчёта для перешедших команд (macroSteps или steps).
 * @return false — шаг надо убрать (DeviceId не меняется).
 */
bool retargetStep(pugi::xml_node operation, const ReplaceContext& context, int& moved) {
    const std::string_view kind = textOf(operation.child("Name"));
    bool keep = true;
    if (kind == "SendCommand") {
        const std::string command = context.mapped(parameter(operation, "Command"));
        keep = !command.empty();
        if (keep) {
            setParameter(operation, "Command", command);
            ++moved;
        } else {
            ++context.report->removed;
        }
    } else if (kind == "SetValue" && parameter(operation, "State") == "Input") {
        keep = std::ranges::find(context.newInputs, parameter(operation, "Value")) != context.newInputs.end();
        if (!keep) {
            ++context.report->inputsLost;
        }
    } else if (kind == "SetValue" && parameter(operation, "State") == "Power") {
        keep = context.newPowerControlled; // вручную — пульт его не включает
    }
    if (keep) {
        setParameter(operation, "DeviceId", context.newId);
    }
    return keep;
}

/// Кнопки-макросы активностей, которые затрагивает замена.
std::set<std::string> activityMacros(const model::RemoteConfig& config,
                                     const std::function<bool(const std::string&)>& included) {
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
    return macros;
}

/// Итог правки макросов.
struct MacroChanges {
    std::string actionLists;       ///< Новый ActionLists.xml; пусто — макросы не менялись
    std::set<std::string> changed; ///< Макросы, шаги которых поменялись
    std::set<std::string> emptied; ///< Макросы, у которых не осталось шагов: их кнопки убираются
};

/**
 * @brief Перевести шаги макросов на новое устройство.
 *
 * Шаги живут в ActionLists.xml, поэтому правятся до UserConfiguration.xml, а применяются
 * только вместе с ним (как в setMacro): документ здесь не меняется.
 */
Result<MacroChanges> replaceInMacros(const ConfigDocument::Impl& impl, const std::set<std::string>& macros,
                                     const ReplaceContext& context) {
    MacroChanges result;
    if (macros.empty()) {
        return result;
    }
    auto files = detail::currentFiles(impl);
    if (!files) {
        return std::unexpected(std::move(files.error()));
    }
    pugi::xml_document lists;
    if (auto loaded = detail::loadDom(lists, files->actionLists); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    for (const std::string& name : macros) {
        pugi::xml_node list = lists.child("Root").find_child_by_attribute("ActionList", "name", name.c_str());
        if (!list) {
            continue;
        }
        for (pugi::xml_node action = list.child("Action"); action;) {
            const pugi::xml_node next = action.next_sibling("Action");
            if (parameter(action.child("Operation"), "DeviceId") == context.oldId) {
                result.changed.insert(name);
                if (!retargetStep(action.child("Operation"), context, context.report->macroSteps)) {
                    list.remove_child(action);
                }
            }
            action = next;
        }
        if (!list.child("Action")) {
            result.emptied.insert(name);
        }
    }
    if (!result.changed.empty()) {
        result.actionLists = detail::saveDom(lists);
    }
    return result;
}

/**
 * @brief Питание: новое включается, если включалось прежнее (или уже само); прежнее — выключено.
 * @return true — активность изменилась.
 */
bool replacePower(pugi::xml_node activity, const std::string& oldId, const std::string& newId) {
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
        (firstOff ? power.insert_child_before("On", firstOff) : power.append_child("On")).text() = newId.c_str();
    }
    return listed;
}

/// Роли активности — новому устройству; true — активность изменилась.
bool replaceRoles(pugi::xml_node activity, const std::string& oldId, const std::string& newId) {
    bool touched = false;
    for (pugi::xml_node role : activity.children("Role")) {
        if (textOf(role.child("DeviceId")) == oldId) {
            role.child("DeviceId").text() = newId.c_str();
            touched = true;
        }
    }
    return touched;
}

/// Шаги запуска: команды — сопоставленные, входы — если есть у нового; true — активность изменилась.
bool replaceEnterActions(pugi::xml_node activity, const ReplaceContext& context) {
    bool touched = false;
    pugi::xml_node enter = activity.child("EnterActions");
    for (pugi::xml_node action = enter.child("Action"); action;) {
        const pugi::xml_node next = action.next_sibling("Action");
        if (parameter(action.child("Operation"), "DeviceId") == context.oldId) {
            touched = true;
            if (!retargetStep(action.child("Operation"), context, context.report->steps)) {
                enter.remove_child(action);
            }
        }
        action = next;
    }
    return touched;
}

/// Кнопки активности, аппаратные и экранные; true — активность изменилась.
bool replaceButtons(pugi::xml_node activity, const ReplaceContext& context, const MacroChanges& macros) {
    bool touched = false;
    DeviceReplacementReport& report = *context.report;
    for (pugi::xml_node group : activity.child("Presentation").children("ControlGroup")) {
        for (pugi::xml_node button = group.child("Button"); button;) {
            const pugi::xml_node next = button.next_sibling("Button");
            const std::string actionId(textOf(button.child("ActionId")));
            const ActionParts parts = split(actionId);
            if (macros.emptied.contains(actionId)) {
                // Макрос слал только команды, которых у нового нет.
                touched = true;
                group.remove_child(button);
                ++report.removed;
            } else if (macros.changed.contains(actionId)) {
                touched = true; // шаги поменялись в ActionLists.xml
            } else if (parts.device == context.oldId) {
                touched = true;
                const std::string command = context.mapped(parts.command);
                if (command.empty()) {
                    group.remove_child(button);
                    ++report.removed;
                } else {
                    button.child("ActionId").text() =
                        (context.newId + "_" + command + "_" + (parts.modifier.empty() ? "Hold" : parts.modifier))
                            .c_str();
                    ++report.buttons;
                }
            }
            button = next;
        }
    }
    return touched;
}

} // namespace

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

    DeviceReplacementReport report;
    ReplaceContext context;
    context.oldId = replacement.oldId;
    context.newId = replacement.newId;
    context.newInputs = newDevice->inputs;
    context.newPowerControlled = newDevice->powerControlled;
    // Таблица команд: заданное пользователем, остальное — подбором.
    context.mapped = [&](const std::string& command) {
        if (const auto it = replacement.commands.find(command); it != replacement.commands.end()) {
            return newDevice->findCommand(it->second) != nullptr ? it->second : std::string();
        }
        return matchDeviceCommand(replacement.newId, command);
    };
    context.report = &report;
    const auto included = [&replacement](const std::string& activityId) {
        return replacement.activities.empty() ||
               std::ranges::find(replacement.activities, activityId) != replacement.activities.end();
    };

    auto macros = replaceInMacros(*impl_, activityMacros(config, included), context);
    if (!macros) {
        return std::unexpected(std::move(macros.error()));
    }

    auto changed = detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        for (pugi::xml_node activity : root.children("Activity")) {
            const std::string id(textOf(activity.child("Id")));
            if (textOf(activity.child("Type")) == "PowerOff" || !included(id)) {
                continue;
            }
            // Все этапы — без короткого замыкания: каждый правит своё.
            bool touched = replacePower(activity, context.oldId, context.newId);
            touched = replaceRoles(activity, context.oldId, context.newId) || touched;
            touched = replaceEnterActions(activity, context) || touched;
            touched = replaceButtons(activity, context, *macros) || touched;
            if (touched) {
                ++report.activities;
            }
        }
        return {};
    });
    if (!changed) {
        return std::unexpected(std::move(changed.error()));
    }
    if (!macros->actionLists.empty()) {
        impl_->actionLists = std::move(macros->actionLists);
    }
    if (replacement.removeOld && activitiesUsingDevice(context.oldId).empty()) {
        if (auto removed = removeDevice(context.oldId); removed) {
            report.oldRemoved = true;
        }
    }
    return report;
}

} // namespace harmony
