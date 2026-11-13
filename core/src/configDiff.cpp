/**
 * @file configDiff.cpp
 * @brief Сравнение двух конфигураций: что изменится на пульте после записи.
 */

#include "harmony/core/configDiff.h"

#include "harmony/core/configDocument.h"

#include <algorithm>
#include <map>
#include <set>

namespace harmony {

namespace {

constexpr const char* kHardGroup = "HardButtons";

/**
 * @brief Список имён: не больше шести, по алфавиту; остальные — числом.
 * @param pattern Шаблон с %1 — списком («new commands: %1»).
 * @param patternMore Тот же с %2 — сколько ещё («new commands: %1 and %2 more»).
 */
Message listMessage(const char* pattern, const char* patternMore, std::vector<std::string> names) {
    constexpr std::size_t kShown = 6;
    std::ranges::sort(names);
    std::string list;
    for (std::size_t i = 0; i < names.size() && i < kShown; ++i) {
        list += (i == 0 ? "" : ", ") + names[i];
    }
    if (names.size() > kShown) {
        return say(patternMore, {list, std::to_string(names.size() - kShown)});
    }
    return say(pattern, {list});
}

/// Аппаратные кнопки: кнопка → действие.
std::map<std::string, std::string> hardKeys(const std::vector<model::ControlGroup>& groups) {
    std::map<std::string, std::string> out;
    for (const auto& group : groups) {
        if (group.name == kHardGroup) {
            for (const auto& button : group.buttons) {
                out[button.name] = button.actionId;
            }
        }
    }
    return out;
}

/// Экранные кнопки целиком: место, подпись, значок, действие — по порядку.
std::vector<std::string> softKeys(const std::vector<model::ControlGroup>& groups) {
    std::vector<std::string> out;
    for (const auto& group : groups) {
        if (group.name != kHardGroup) {
            for (const auto& button : group.buttons) {
                out.push_back(std::to_string(button.position) + '|' + button.label + '|' +
                              button.icon + '|' + button.actionId);
            }
        }
    }
    std::ranges::sort(out);
    return out;
}

/// Сколько аппаратных кнопок назначено иначе.
std::size_t keyChanges(const std::map<std::string, std::string>& a,
                       const std::map<std::string, std::string>& b) {
    std::set<std::string> keys;
    for (const auto& [key, action] : a) {
        keys.insert(key);
    }
    for (const auto& [key, action] : b) {
        keys.insert(key);
    }
    return static_cast<std::size_t>(std::ranges::count_if(keys, [&](const std::string& key) {
        const auto ia = a.find(key);
        const auto ib = b.find(key);
        return (ia == a.end() ? std::string() : ia->second) !=
               (ib == b.end() ? std::string() : ib->second);
    }));
}

std::vector<std::string> commandNames(const model::Device& device) {
    std::vector<std::string> out;
    for (const auto& command : device.commands) {
        if (std::ranges::find(out, command.name) == out.end()) {
            out.push_back(command.name);
        }
    }
    return out;
}

std::vector<Message> compareDevice(const ConfigDocument& before, const model::Device& a,
                                   const ConfigDocument& after, const model::Device& b) {
    std::vector<Message> d;
    if (a.displayName() != b.displayName()) {
        d.push_back(say("name: %1 → %2", {a.displayName(), b.displayName()}));
    }
    if (a.type != b.type) {
        d.push_back(say("type: %1 → %2", {a.type, b.type}));
    }
    if (a.manufacturer != b.manufacturer || a.model != b.model) {
        d.push_back(say("model: %1 → %2",
                        {a.manufacturer + " " + a.model, b.manufacturer + " " + b.model}));
    }
    const auto ca = commandNames(a);
    const auto cb = commandNames(b);
    std::vector<std::string> added;
    std::vector<std::string> removed;
    std::vector<std::string> changed;
    for (const auto& name : cb) {
        if (std::ranges::find(ca, name) == ca.end()) {
            added.push_back(name);
        } else {
            // Сигнал неизвестен (нет ИК-таблиц) — сравнивать нечего.
            const auto old = before.commandSignal(a.id, name);
            const auto next = after.commandSignal(b.id, name);
            if (old && next && *old != *next) {
                changed.push_back(name);
            }
        }
    }
    for (const auto& name : ca) {
        if (std::ranges::find(cb, name) == cb.end()) {
            removed.push_back(name);
        }
    }
    // Шаблоны — литералами прямо здесь: их находит сборщик строк для перевода.
    if (!added.empty()) {
        d.push_back(listMessage("new commands: %1", "new commands: %1 and %2 more", added));
    }
    if (!removed.empty()) {
        d.push_back(
            listMessage("commands removed: %1", "commands removed: %1 and %2 more", removed));
    }
    if (!changed.empty()) {
        d.push_back(listMessage("other signal: %1", "other signal: %1 and %2 more", changed));
    }
    if (const auto keys = keyChanges(hardKeys(a.controlGroups), hardKeys(b.controlGroups));
        keys > 0) {
        d.push_back(say("keys changed: %1", {std::to_string(keys)}));
    }
    if (softKeys(a.controlGroups) != softKeys(b.controlGroups)) {
        d.push_back(say("screen buttons changed"));
    }
    if (a.powerControlled != b.powerControlled || a.inputs != b.inputs) {
        d.push_back(say("power or input switching changed"));
    }
    return d;
}

/// Имена устройств по id, по алфавиту (удалённые — id).
std::vector<std::string> deviceNames(const model::RemoteConfig& config,
                                     const std::vector<std::string>& ids) {
    std::vector<std::string> out;
    for (const auto& id : ids) {
        const model::Device* device = config.findDevice(id);
        out.push_back(device != nullptr ? device->displayName() : id);
    }
    std::ranges::sort(out);
    return out;
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

std::vector<Message> compareActivity(const model::RemoteConfig& before, const model::Activity& a,
                                     std::size_t positionA, const model::RemoteConfig& after,
                                     const model::Activity& b, std::size_t positionB) {
    std::vector<Message> d;
    if (a.label != b.label) {
        d.push_back(say("name: %1 → %2", {a.label, b.label}));
    }
    if (a.type != b.type) {
        d.push_back(say("type: %1 → %2", {a.type, b.type}));
    }
    if (positionA != positionB) {
        d.push_back(say("place in the list: %1 → %2",
                        {std::to_string(positionA), std::to_string(positionB)}));
    }
    const auto onA = deviceNames(before, a.powerOn);
    const auto onB = deviceNames(after, b.powerOn);
    if (onA != onB) {
        d.push_back(say("switches on: %1 → %2", {joined(onA), joined(onB)}));
    }
    auto roles = [](const model::Activity& activity) {
        std::vector<std::string> out;
        for (const auto& role : activity.roles) {
            out.push_back(role.name + ':' + role.deviceId);
        }
        std::ranges::sort(out);
        return out;
    };
    if (roles(a) != roles(b)) {
        d.push_back(say("roles (control, volume, picture) changed"));
    }
    auto inputs = [](const model::Activity& activity) {
        auto out = activity.inputs;
        std::ranges::sort(out);
        return out;
    };
    if (inputs(a) != inputs(b)) {
        d.push_back(say("inputs changed"));
    }
    if (const auto keys = keyChanges(hardKeys(a.controlGroups), hardKeys(b.controlGroups));
        keys > 0) {
        d.push_back(say("keys changed: %1", {std::to_string(keys)}));
    }
    if (softKeys(a.controlGroups) != softKeys(b.controlGroups)) {
        d.push_back(say("screen buttons changed"));
    }
    return d;
}

/// Активность и её место в списке (без «кнопки Off»), начиная с 1.
const model::Activity* findActivity(const model::RemoteConfig& config, const std::string& id,
                                    std::size_t& position) {
    position = 0;
    for (const auto& activity : config.activities) {
        if (!activity.isPowerOff()) {
            ++position;
        }
        if (activity.id == id) {
            return &activity;
        }
    }
    return nullptr;
}

} // namespace

std::vector<ConfigChange> compareConfigurations(const ConfigDocument& before,
                                                const ConfigDocument& after) {
    const model::RemoteConfig& from = before.model();
    const model::RemoteConfig& to = after.model();
    std::vector<ConfigChange> out;

    for (const auto& device : to.devices) {
        ConfigChange change;
        change.area = ConfigChange::Area::Device;
        change.item = device.displayName();
        const model::Device* old = from.findDevice(device.id);
        if (old == nullptr) {
            change.kind = ConfigChange::Kind::Added;
            change.details.push_back(
                say("commands: %1", {std::to_string(commandNames(device).size())}));
            out.push_back(std::move(change));
            continue;
        }
        change.details = compareDevice(before, *old, after, device);
        if (!change.details.empty()) {
            out.push_back(std::move(change));
        }
    }
    for (const auto& device : from.devices) {
        if (to.findDevice(device.id) == nullptr) {
            out.push_back({ConfigChange::Kind::Removed,
                           ConfigChange::Area::Device,
                           device.displayName(),
                           {}});
        }
    }

    for (const auto& activity : to.activities) {
        if (activity.isPowerOff()) {
            continue; // «кнопка Off» меняется вместе с устройствами — отдельно не показываем
        }
        std::size_t positionB = 0;
        std::size_t positionA = 0;
        findActivity(to, activity.id, positionB);
        ConfigChange change;
        change.area = ConfigChange::Area::Activity;
        change.item = activity.label;
        const model::Activity* old = findActivity(from, activity.id, positionA);
        if (old == nullptr) {
            change.kind = ConfigChange::Kind::Added;
            out.push_back(std::move(change));
            continue;
        }
        change.details = compareActivity(from, *old, positionA, to, activity, positionB);
        if (!change.details.empty()) {
            out.push_back(std::move(change));
        }
    }
    for (const auto& activity : from.activities) {
        if (!activity.isPowerOff() && to.findActivity(activity.id) == nullptr) {
            out.push_back(
                {ConfigChange::Kind::Removed, ConfigChange::Area::Activity, activity.label, {}});
        }
    }
    return out;
}

} // namespace harmony
