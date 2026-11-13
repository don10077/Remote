/**
 * @file configDocumentMacros.cpp
 * @brief Кнопки-макросы: несколько шагов в одном ActionList (см. macro.h).
 *
 * Шаги живут в ActionLists.xml, кнопка — в UserConfiguration.xml; правка меняет оба
 * файла вместе: сначала готовится новый ActionLists.xml, затем правится кнопка, и
 * только при успехе обоих шагов документ меняется.
 */

#include "actionLists.h"
#include "configDocumentImpl.h"
#include "harmony/core/macro.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>

namespace harmony {

namespace {

using detail::appendText;

constexpr std::string_view kMacroMark = "_macro_";
constexpr const char* kSoftGroup = "Misc";
constexpr const char* kHardGroup = "HardButtons";

std::string parameter(pugi::xml_node operation, const char* name) {
    return std::string(detail::textOf(operation.find_child_by_attribute("Parameter", "name", name)));
}

int number(const std::string& text) {
    int value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && end == text.data() + text.size() ? std::max(0, value) : 0;
}

/// <Action> → шаг макроса.
MacroStep readStep(pugi::xml_node action) {
    const pugi::xml_node operation = action.child("Operation");
    const std::string_view name = detail::textOf(operation.child("Name"));
    MacroStep out;
    out.deviceId = parameter(operation, "DeviceId");
    StateStep& step = out.step;
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
        std::ostringstream text;
        action.print(text, "", pugi::format_raw);
        step.raw = text.str();
    }
    return out;
}

/// Шаг → <Action> в списке.
void writeStep(pugi::xml_node list, const MacroStep& item) {
    const StateStep& step = item.step;
    if (step.kind == StateStep::Kind::Other) {
        pugi::xml_document fragment;
        if (fragment.load_string(step.raw.c_str()) && fragment.first_child()) {
            list.append_copy(fragment.first_child());
        }
        return;
    }
    pugi::xml_node action = list.append_child("Action");
    appendText(action, "Target", "Device");
    pugi::xml_node operation = action.append_child("Operation");
    auto param = [&operation](const char* name, const std::string& value) {
        appendText(operation, "Parameter", value).append_attribute("name") = name;
    };
    switch (step.kind) {
    case StateStep::Kind::Command:
        appendText(operation, "Name", "SendCommand");
        param("DeviceId", item.deviceId);
        param("Command", step.command);
        param("Modifier", "Press");
        if (step.durationMs > 0) {
            param("Duration", std::to_string(step.durationMs));
        }
        break;
    case StateStep::Kind::Delay:
        appendText(operation, "Name", "SendDelay");
        param("DeviceId", item.deviceId);
        param("Delay", std::to_string(std::max(0, step.delayMs)));
        break;
    case StateStep::Kind::SetValue:
        appendText(operation, "Name", "SetValue");
        param("DeviceId", item.deviceId);
        param("State", step.state);
        param("Value", step.value);
        break;
    case StateStep::Kind::Other:
        break;
    }
}

/// Шаги подходят владельцу кнопки.
Result<void> checkSteps(const model::RemoteConfig& config, const ButtonOwner& owner, const Macro& steps) {
    if (steps.empty()) {
        return makeError(ErrorCode::Format, "the macro has no steps");
    }
    const model::Activity* activity = owner.isDevice() ? nullptr : config.findActivity(owner.id);
    if (owner.isDevice() ? config.findDevice(owner.id) == nullptr : activity == nullptr) {
        return makeError(ErrorCode::NotFound, owner.isDevice() ? "no device %1" : "no activity %1", {owner.id});
    }
    for (const MacroStep& item : steps) {
        const model::Device* device = config.findDevice(item.deviceId);
        if (device == nullptr) {
            return makeError(ErrorCode::NotFound, "no device %1", {item.deviceId});
        }
        if (owner.isDevice() && item.deviceId != owner.id) {
            return makeError(ErrorCode::Format, "device buttons send commands only to the device itself");
        }
        if (item.step.kind != StateStep::Kind::Command) {
            continue;
        }
        if (activity != nullptr &&
            std::ranges::find(activity->powerOn, item.deviceId) == activity->powerOn.end()) {
            return makeError(ErrorCode::Format, "“%1” is not turned on in this activity — the button will not work",
                             {device->displayName()});
        }
        if (device->findCommand(item.step.command) == nullptr) {
            return makeError(ErrorCode::NotFound, "“%1” has no command “%2”", {device->displayName(), item.step.command});
        }
    }
    return {};
}

/// Действие кнопки сейчас (пусто — нет кнопки).
std::string currentAction(pugi::xml_node owner, bool soft, const std::string& key, int position) {
    for (pugi::xml_node group : owner.child("Presentation").children("ControlGroup")) {
        const bool hard = std::string_view(group.attribute("name").value()) == kHardGroup;
        if (hard == soft) {
            continue;
        }
        for (pugi::xml_node button : group.children("Button")) {
            const bool match = soft ? button.child("Position").text().as_int(-1) == position
                                    : key == button.attribute("name").value();
            if (match) {
                return std::string(detail::textOf(button.child("ActionId")));
            }
        }
    }
    return {};
}

/// Группа кнопок владельца; создаётся при необходимости (экранные — перед аппаратными).
pugi::xml_node ensureGroup(pugi::xml_node owner, const char* name) {
    pugi::xml_node presentation = owner.child("Presentation");
    if (!presentation) {
        pugi::xml_node after = owner.child("Properties") ? owner.child("Properties") : owner.child("Model");
        presentation = after ? owner.insert_child_after("Presentation", after) : owner.append_child("Presentation");
    }
    if (pugi::xml_node found = presentation.find_child_by_attribute("ControlGroup", "name", name)) {
        return found;
    }
    pugi::xml_node hard = presentation.find_child_by_attribute("ControlGroup", "name", kHardGroup);
    pugi::xml_node created = std::string_view(name) == kSoftGroup && hard
                                 ? presentation.insert_child_before("ControlGroup", hard)
                                 : presentation.append_child("ControlGroup");
    created.append_attribute("name") = name;
    return created;
}

void setChild(pugi::xml_node parent, const char* name, const std::string& text) {
    pugi::xml_node node = parent.child(name);
    if (!node) {
        node = parent.append_child(name);
    }
    node.text() = text.empty() ? std::string(detail::kEmptyText).c_str() : text.c_str();
}

} // namespace

bool isMacroAction(std::string_view actionId) {
    const auto at = actionId.rfind(kMacroMark);
    if (at == std::string_view::npos || at == 0) {
        return false;
    }
    const std::string_view tail = actionId.substr(at + kMacroMark.size());
    return !tail.empty() && std::ranges::all_of(tail, [](char c) { return c >= '0' && c <= '9'; });
}

Result<Macro> ConfigDocument::macro(const std::string& actionId) const {
    pugi::xml_document lists;
    if (impl_->actionLists.empty() || !detail::loadDom(lists, impl_->actionLists)) {
        return makeError(ErrorCode::NotFound, "no macro %1", {actionId});
    }
    const pugi::xml_node list =
        lists.child("Root").find_child_by_attribute("ActionList", "name", actionId.c_str());
    if (!list) {
        return makeError(ErrorCode::NotFound, "no macro %1", {actionId});
    }
    Macro steps;
    for (pugi::xml_node action : list.children("Action")) {
        steps.push_back(readStep(action));
    }
    return steps;
}

namespace {

/// Общая часть setHardMacro/setSoftMacro: шаги — в ActionLists.xml, кнопка — @p place.
Result<void> setMacro(ConfigDocument::Impl& impl, const ButtonOwner& owner, bool soft, const std::string& key,
                      int position, const std::string& label, const Macro& steps) {
    if (auto checked = checkSteps(impl.model, owner, steps); !checked) {
        return checked;
    }
    if (soft && label.empty()) {
        return makeError(ErrorCode::Format, "a screen macro button needs a label");
    }
    if (!soft && key.empty()) {
        return makeError(ErrorCode::Format, "no remote button specified");
    }

    // ActionLists.xml — как он будет сохранён сейчас (с действиями всех кнопок).
    auto files = detail::currentFiles(impl);
    if (!files) {
        return std::unexpected(std::move(files.error()));
    }
    pugi::xml_document lists;
    if (auto loaded = detail::loadDom(lists, files->actionLists); !loaded) {
        return loaded;
    }
    pugi::xml_node root = lists.child("Root");

    // Имя: прежний макрос этой кнопки — то же; иначе первое свободное.
    pugi::xml_document dom;
    if (auto loaded = detail::loadDom(dom, impl.xml); !loaded) {
        return loaded;
    }
    const pugi::xml_node ownerNode =
        detail::findById(dom.child("Root"), owner.isDevice() ? "Device" : "Activity", owner.id);
    std::string name = currentAction(ownerNode, soft, key, position);
    const std::string prefix = owner.id + std::string(kMacroMark);
    if (!isMacroAction(name) || !name.starts_with(prefix)) {
        std::set<std::string> taken;
        for (pugi::xml_node list : root.children("ActionList")) {
            taken.insert(list.attribute("name").value());
        }
        int n = 1;
        while (taken.contains(prefix + std::to_string(n))) {
            ++n;
        }
        name = prefix + std::to_string(n);
    }
    pugi::xml_node list = root.find_child_by_attribute("ActionList", "name", name.c_str());
    if (list) {
        while (list.first_child()) {
            list.remove_child(list.first_child());
        }
    } else {
        pugi::xml_node before = root.child("Properties");
        list = before ? root.insert_child_before("ActionList", before) : root.append_child("ActionList");
        list.append_attribute("name") = name.c_str();
    }
    for (const MacroStep& step : steps) {
        writeStep(list, step);
    }
    std::string updated = detail::saveDom(lists);

    auto changed = detail::mutate(impl, [&](pugi::xml_node xmlRoot) -> Result<void> {
        pugi::xml_node node = detail::findById(xmlRoot, owner.isDevice() ? "Device" : "Activity", owner.id);
        if (soft) {
            pugi::xml_node group = ensureGroup(node, kSoftGroup);
            pugi::xml_node button;
            for (pugi::xml_node item : group.children("Button")) {
                if (item.child("Position").text().as_int(-1) == position) {
                    button = item;
                }
            }
            if (!button) {
                button = group.append_child("Button");
                appendText(button, "Label", label);
                appendText(button, "Position", std::to_string(position));
            }
            button.remove_child("Icon");
            setChild(button, "Label", label);
            setChild(button, "ActionId", name);
        } else {
            pugi::xml_node group = ensureGroup(node, kHardGroup);
            pugi::xml_node button = group.find_child_by_attribute("Button", "name", key.c_str());
            if (!button) {
                button = group.append_child("Button");
                button.append_attribute("name") = key.c_str();
                appendText(button, "Label", "");
            }
            setChild(button, "ActionId", name);
        }
        return {};
    });
    if (!changed) {
        return changed;
    }
    impl.actionLists = std::move(updated);
    return {};
}

} // namespace

Result<void> ConfigDocument::setHardMacro(const ButtonOwner& owner, const std::string& key, const Macro& steps) {
    return setMacro(*impl_, owner, false, key, -1, {}, steps);
}

Result<void> ConfigDocument::setSoftMacro(const ButtonOwner& owner, int position, const std::string& label,
                                          const Macro& steps) {
    if (position < 0) {
        return makeError(ErrorCode::Format, "invalid screen position");
    }
    return setMacro(*impl_, owner, true, {}, position, label, steps);
}

} // namespace harmony
