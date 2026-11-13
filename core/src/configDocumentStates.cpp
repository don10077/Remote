/**
 * @file configDocumentStates.cpp
 * @brief Питание и входы устройства: чтение, правка и заготовка по именам команд.
 */

#include "configDocumentImpl.h"
#include "harmony/core/deviceStates.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace harmony {

namespace {

using detail::appendText;
using detail::textOf;

constexpr const char* kPower = "Power";
constexpr const char* kInput = "Input";

std::string parameter(pugi::xml_node operation, const char* name) {
    return std::string(textOf(operation.find_child_by_attribute("Parameter", "name", name)));
}

int number(const std::string& text) {
    try {
        return std::max(0, std::stoi(text));
    } catch (...) {
        return 0;
    }
}

/// Действия (<Action>…) внутри узла → шаги.
StateSequence readSequence(pugi::xml_node parent) {
    StateSequence steps;
    for (pugi::xml_node action : parent.children("Action")) {
        const pugi::xml_node operation = action.child("Operation");
        const std::string_view name = textOf(operation.child("Name"));
        StateStep step;
        if (name == "SendCommand") {
            step.kind = StateStep::Kind::Command;
            step.command = parameter(operation, "Command");
            step.durationMs = number(parameter(operation, "Duration"));
        } else if (name == "SendDelay") {
            step.kind = StateStep::Kind::Delay;
            step.delayMs = number(detail::delayText(operation));
        } else if (name == "SetValue") {
            step.kind = StateStep::Kind::SetValue;
            step.state = parameter(operation, "State");
            step.value = parameter(operation, "Value");
        } else {
            step.kind = StateStep::Kind::Other;
            std::ostringstream out;
            action.print(out, "", pugi::format_raw);
            step.raw = out.str();
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

/// Шаги → действия в узле @p parent (перед @p before, если задан).
void writeSequence(pugi::xml_node parent, const std::string& deviceId, const StateSequence& steps) {
    for (const StateStep& step : steps) {
        switch (step.kind) {
        case StateStep::Kind::Command: {
            detail::appendSendCommand(parent, deviceId, step.command);
            if (step.durationMs > 0) {
                appendText(parent.last_child().child("Operation"), "Parameter",
                           std::to_string(step.durationMs))
                    .append_attribute("name") = "Duration";
            }
            break;
        }
        case StateStep::Kind::Delay: {
            pugi::xml_node action = parent.append_child("Action");
            appendText(action, "Target", "Device");
            pugi::xml_node operation = action.append_child("Operation");
            appendText(operation, "Name", "SendDelay");
            appendText(operation, "Parameter", deviceId).append_attribute("name") = "DeviceId";
            appendText(operation, "Parameter", std::to_string(std::max(0, step.delayMs)))
                .append_attribute("name") = "Delay";
            break;
        }
        case StateStep::Kind::SetValue:
            detail::appendSetValue(parent, deviceId, step.state, step.value);
            break;
        case StateStep::Kind::Other: {
            pugi::xml_document fragment;
            if (fragment.load_string(step.raw.c_str()) && fragment.first_child()) {
                parent.append_copy(fragment.first_child());
            }
            break;
        }
        }
    }
}

pugi::xml_node findState(pugi::xml_node device, const char* id) {
    for (pugi::xml_node state : device.child("States").children("State")) {
        if (textOf(state.child("Id")) == id) {
            return state;
        }
    }
    return {};
}

/// SetAction с именем @p name в DiscreteActions.
pugi::xml_node setAction(pugi::xml_node state, std::string_view name) {
    for (pugi::xml_node set : state.child("DiscreteActions").children("SetAction")) {
        if (textOf(set.child("Name")) == name) {
            return set;
        }
    }
    return {};
}

/// Проверяет шаги: команды — у устройства, паузы — неотрицательные.
Result<void> checkSequence(const model::Device& device, const StateSequence& steps,
                           const std::string& where) {
    if (steps.empty()) {
        return makeError(ErrorCode::Format, "no steps for “%1”", {where});
    }
    for (const StateStep& step : steps) {
        if (step.kind == StateStep::Kind::Command && device.findCommand(step.command) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             "“%1” uses the command “%2”, which the device does not have",
                             {where, step.command});
        }
    }
    return {};
}

/// Новый узел <State> на месте прежнего (или в нужном месте <States>).
pugi::xml_node replaceState(pugi::xml_node device, const char* id) {
    pugi::xml_node states = device.child("States");
    if (!states) {
        pugi::xml_node before =
            device.child("Numeric") ? device.child("Numeric") : device.child("Commands");
        states =
            before ? device.insert_child_before("States", before) : device.append_child("States");
    }
    pugi::xml_node old = findState(device, id);
    pugi::xml_node state;
    if (old) {
        state = states.insert_child_before("State", old);
        states.remove_child(old);
    } else if (std::string_view(id) == kPower) {
        state = states.prepend_child("State"); // питание — первым, как у Logitech
    } else if (pugi::xml_node power = findState(device, kPower)) {
        state = states.insert_child_after("State", power);
    } else {
        state = states.prepend_child("State");
    }
    appendText(state, "Id", id);
    return state;
}

void removeState(pugi::xml_node device, const char* id) {
    if (pugi::xml_node old = findState(device, id)) {
        pugi::xml_node states = old.parent();
        states.remove_child(old);
        if (!states.first_child()) {
            device.remove_child(states);
        }
    }
}

/// «Выключить всё» больше не выключает устройство (питание — вручную).
void disablePowerOff(pugi::xml_node root, pugi::xml_node device, const std::string& deviceId) {
    if (pugi::xml_node properties = device.child("Properties")) {
        detail::setProperty(properties, "ManualPower", "true");
    }
    for (pugi::xml_node activity : root.children("Activity")) {
        if (textOf(activity.child("Type")) != "PowerOff") {
            continue;
        }
        pugi::xml_node enter = activity.child("EnterActions");
        for (pugi::xml_node action = enter.child("Action"); action;) {
            const pugi::xml_node next = action.next_sibling("Action");
            const pugi::xml_node operation = action.child("Operation");
            if (textOf(operation.child("Name")) == "SetValue" &&
                parameter(operation, "DeviceId") == deviceId &&
                parameter(operation, "State") == kPower) {
                enter.remove_child(action);
            }
            action = next;
        }
    }
}

/// «Выключить всё» уже выключает устройство.
bool powerOffHas(pugi::xml_node root, const std::string& deviceId) {
    for (pugi::xml_node activity : root.children("Activity")) {
        if (textOf(activity.child("Type")) != "PowerOff") {
            continue;
        }
        for (pugi::xml_node action : activity.child("EnterActions").children("Action")) {
            const pugi::xml_node operation = action.child("Operation");
            if (textOf(operation.child("Name")) == "SetValue" &&
                parameter(operation, "DeviceId") == deviceId &&
                parameter(operation, "State") == kPower) {
                return true;
            }
        }
    }
    return false;
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

Result<DeviceStates> ConfigDocument::deviceStates(const std::string& deviceId) const {
    pugi::xml_document dom;
    if (auto loaded = detail::loadDom(dom, impl_->xml); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    const pugi::xml_node device = detail::findById(dom.child("Root"), "Device", deviceId);
    if (!device) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    DeviceStates states;
    if (const pugi::xml_node power = findState(device, kPower)) {
        states.power.delayMs = number(std::string(textOf(power.child("Delay"))));
        if (power.child("DiscreteActions")) {
            states.power.mode = PowerSetup::Mode::Discrete;
            states.power.on = readSequence(setAction(power, "On"));
            states.power.off = readSequence(setAction(power, "Off"));
        } else if (power.child("RelativeActions")) {
            states.power.mode = PowerSetup::Mode::Toggle;
            states.power.toggle = readSequence(power.child("RelativeActions").child("NextAction"));
        }
    }
    if (const pugi::xml_node input = findState(device, kInput)) {
        states.input.delayMs = number(std::string(textOf(input.child("Delay"))));
        const bool discrete = static_cast<bool>(input.child("DiscreteActions"));
        states.input.mode = discrete ? InputSetup::Mode::Discrete : InputSetup::Mode::Cycle;
        for (const pugi::xml_node value : input.children("Value")) {
            InputSetup::Input item;
            item.name = std::string(textOf(value));
            if (discrete) {
                item.select = readSequence(setAction(input, item.name));
            }
            states.input.inputs.push_back(std::move(item));
        }
        if (!discrete) {
            const pugi::xml_node relative = input.child("RelativeActions");
            states.input.next = readSequence(relative.child("NextAction"));
            states.input.prev = readSequence(relative.child("PrevAction"));
        }
    }
    return states;
}

Result<void> ConfigDocument::setDeviceStates(const std::string& deviceId,
                                             const DeviceStates& states) {
    const model::Device* device = impl_->model.findDevice(deviceId);
    if (device == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    // Проверка — до правки: при ошибке документ не меняется.
    const PowerSetup& power = states.power;
    if (power.mode == PowerSetup::Mode::Discrete) {
        if (auto checked = checkSequence(*device, power.on, "On"); !checked) {
            return checked;
        }
        if (auto checked = checkSequence(*device, power.off, "Off"); !checked) {
            return checked;
        }
    } else if (power.mode == PowerSetup::Mode::Toggle) {
        if (auto checked = checkSequence(*device, power.toggle, "Power"); !checked) {
            return checked;
        }
    }
    const InputSetup& input = states.input;
    if (input.mode != InputSetup::Mode::None) {
        if (input.inputs.empty()) {
            return makeError(ErrorCode::Format, "no inputs");
        }
        std::set<std::string> names;
        for (const auto& item : input.inputs) {
            if (item.name.empty()) {
                return makeError(ErrorCode::Format, "an input has no name");
            }
            if (!names.insert(item.name).second) {
                return makeError(ErrorCode::Format, "two inputs are called “%1”", {item.name});
            }
            if (input.mode == InputSetup::Mode::Discrete) {
                if (auto checked = checkSequence(*device, item.select, item.name); !checked) {
                    return checked;
                }
            }
        }
        if (input.mode == InputSetup::Mode::Cycle) {
            if (auto checked = checkSequence(*device, input.next, "Next input"); !checked) {
                return checked;
            }
            if (!input.prev.empty()) {
                if (auto checked = checkSequence(*device, input.prev, "Previous input"); !checked) {
                    return checked;
                }
            }
        }
    }

    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node node = detail::findById(root, "Device", deviceId);
        // --- Питание ---
        if (power.mode == PowerSetup::Mode::Manual) {
            removeState(node, kPower);
            disablePowerOff(root, node, deviceId);
        } else {
            pugi::xml_node state = replaceState(node, kPower);
            appendText(state, "Value", "Off");
            appendText(state, "Value", "On");
            appendText(state, "Delay", std::to_string(std::max(0, power.delayMs)));
            if (power.mode == PowerSetup::Mode::Discrete) {
                pugi::xml_node actions = state.append_child("DiscreteActions");
                for (const auto& [name, steps] :
                     {std::pair{"Off", &power.off}, std::pair{"On", &power.on}}) {
                    pugi::xml_node set = actions.append_child("SetAction");
                    writeSequence(set, deviceId, *steps);
                    appendText(set, "Name", name);
                }
            } else {
                writeSequence(state.append_child("RelativeActions").append_child("NextAction"),
                              deviceId, power.toggle);
            }
            if (powerOffHas(root, deviceId)) {
                if (pugi::xml_node properties = node.child("Properties")) {
                    detail::setProperty(properties, "ManualPower", "false");
                }
            } else {
                detail::enablePowerOff(root, node, deviceId);
            }
        }
        // --- Входы ---
        if (input.mode == InputSetup::Mode::None) {
            removeState(node, kInput);
        } else {
            pugi::xml_node state = replaceState(node, kInput);
            for (const auto& item : input.inputs) {
                appendText(state, "Value", item.name);
            }
            appendText(state, "Delay", std::to_string(std::max(0, input.delayMs)));
            if (input.mode == InputSetup::Mode::Discrete) {
                pugi::xml_node actions = state.append_child("DiscreteActions");
                for (const auto& item : input.inputs) {
                    pugi::xml_node set = actions.append_child("SetAction");
                    writeSequence(set, deviceId, item.select);
                    appendText(set, "Name", item.name);
                }
            } else {
                pugi::xml_node actions = state.append_child("RelativeActions");
                writeSequence(actions.append_child("NextAction"), deviceId, input.next);
                if (!input.prev.empty()) {
                    writeSequence(actions.append_child("PrevAction"), deviceId, input.prev);
                }
            }
        }
        return {};
    });
}

DeviceStates guessDeviceStates(const std::vector<std::string>& commands,
                               const std::string& deviceType) {
    std::map<std::string, std::string> byLower; // имя в нижнем регистре → как есть
    for (const auto& command : commands) {
        byLower.emplace(lower(command), command);
    }
    auto find = [&byLower](std::initializer_list<const char*> names) -> std::string {
        for (const char* name : names) {
            if (const auto it = byLower.find(name); it != byLower.end()) {
                return it->second;
            }
        }
        return {};
    };
    const bool slow = deviceType == "Television" || deviceType == "Projector" ||
                      deviceType == "TvDvd" || deviceType == "TvDvdVcr" || deviceType == "TvHdd" ||
                      deviceType == "TvVcr";

    DeviceStates states;
    const std::string on = find({"poweron", "power on", "on"});
    const std::string off = find({"poweroff", "power off", "off"});
    const std::string toggle = find({"powertoggle", "power", "power toggle", "standby"});
    if (!on.empty() && !off.empty()) {
        states.power.mode = PowerSetup::Mode::Discrete;
        states.power.on = {StateStep::press(on)};
        states.power.off = {StateStep::press(off)};
    } else if (!toggle.empty()) {
        states.power.mode = PowerSetup::Mode::Toggle;
        states.power.toggle = {StateStep::press(toggle)};
    }
    if (states.power.mode != PowerSetup::Mode::Manual) {
        states.power.delayMs = slow ? 8000 : 3000;
    }

    // Входы: «InputHdmi1» → вход «HDMI1» (имя — то, что после «Input», заглавными).
    for (const auto& command : commands) {
        const std::string low = lower(command);
        if (low.size() <= 5 || !low.starts_with("input")) {
            continue;
        }
        const std::string rest = command.substr(5);
        const std::string restLow = low.substr(5);
        if (restLow == "next" || restLow == "prev" || restLow == "previous" ||
            restLow == "toggle" || restLow == "select" || restLow == "up" || restLow == "down") {
            continue;
        }
        std::string name;
        for (const char c : rest) {
            if (c != ' ' && c != '_' && c != '-') {
                name += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        if (!name.empty()) {
            states.input.inputs.push_back({name, {StateStep::press(command)}});
        }
    }
    if (!states.input.inputs.empty()) {
        states.input.mode = InputSetup::Mode::Discrete;
    } else if (const std::string next =
                   find({"inputnext", "input", "source", "inputselect", "input toggle"});
               !next.empty()) {
        states.input.mode = InputSetup::Mode::Cycle;
        states.input.next = {StateStep::press(next)};
        const std::string prev = find({"inputprev", "inputprevious"});
        if (!prev.empty()) {
            states.input.prev = {StateStep::press(prev)};
        }
        states.input.inputs = {{"Input 1", {}}, {"Input 2", {}}};
    }
    return states;
}

} // namespace harmony
