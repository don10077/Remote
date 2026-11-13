/**
 * @file configDocumentCheck.cpp
 * @brief Проверка конфигурации и автоматическое исправление найденного.
 *
 * Проверяется то, что пульт молча не выполнит или выполнит не так:
 * - кнопки (аппаратные и экранные) шлют команду, которой у устройства нет,
 *   или удалённому устройству; две экранные кнопки на одном месте;
 * - действия при запуске активности и описания питания/входов устройства
 *   ссылаются на несуществующие команды или устройства;
 * - активность не говорит, включено ли устройство, или не включает устройство,
 *   которому шлют команды её кнопки; нет роли DEFAULT; роль у двух устройств;
 * - одинаковые id и имена, пустые имена, устройств больше, чем помнит пульт.
 */

#include "configDocumentImpl.h"
#include "harmony/core/buttonIcons.h"
#include "harmony/core/macro.h"

#include <algorithm>
#include <map>
#include <set>

namespace harmony {

namespace {

constexpr const char* kHardGroup = "HardButtons";

/// Действие кнопки «<устройство>_<команда>_<модификатор>» по частям.
struct ActionRef {
    std::string device;
    std::string command;
};

ActionRef splitAction(const std::string& actionId) {
    const auto first = actionId.find('_');
    const auto last = actionId.rfind('_');
    if (first == std::string::npos) {
        return {actionId, {}};
    }
    if (last == first) {
        return {actionId.substr(0, first), actionId.substr(first + 1)};
    }
    return {actionId.substr(0, first), actionId.substr(first + 1, last - first - 1)};
}

/// Параметр операции <Parameter name="…">.
std::string parameter(pugi::xml_node operation, const char* name) {
    return std::string(
        detail::textOf(operation.find_child_by_attribute("Parameter", "name", name)));
}

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

/// Имя без пробелов по краям.
std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

/// Экранные кнопки владельца: все группы, кроме аппаратных.
std::vector<const model::Button*> softButtons(const std::vector<model::ControlGroup>& groups) {
    std::vector<const model::Button*> out;
    for (const auto& group : groups) {
        if (group.name != kHardGroup) {
            for (const auto& button : group.buttons) {
                out.push_back(&button);
            }
        }
    }
    return out;
}

std::vector<const model::Button*> hardButtons(const std::vector<model::ControlGroup>& groups) {
    std::vector<const model::Button*> out;
    for (const auto& group : groups) {
        if (group.name == kHardGroup) {
            for (const auto& button : group.buttons) {
                out.push_back(&button);
            }
        }
    }
    return out;
}

/// Все <Operation> внутри узла (на любой глубине).
std::vector<pugi::xml_node> operations(pugi::xml_node node) {
    std::vector<pugi::xml_node> out;
    for (pugi::xml_node child : node.children()) {
        if (std::string_view(child.name()) == "Operation") {
            out.push_back(child);
        } else {
            auto inner = operations(child);
            out.insert(out.end(), inner.begin(), inner.end());
        }
    }
    return out;
}

/// Собирает проблемы; держит конфигурацию и её дерево XML.
class Checker {
public:
    Checker(const model::RemoteConfig& config, pugi::xml_node root)
        : config_(config), root_(root) {}

    std::vector<Problem> run() {
        checkConfig();
        for (const auto& device : config_.devices) {
            checkDevice(device);
        }
        for (const auto& activity : config_.activities) {
            checkActivity(activity);
        }
        // Сначала ошибки, внутри — порядок конфигурации.
        std::ranges::stable_sort(problems_, [](const Problem& a, const Problem& b) {
            return a.severity == Problem::Severity::Error && b.severity != Problem::Severity::Error;
        });
        return std::move(problems_);
    }

private:
    Problem& add(Problem::Severity severity, Problem::Where where, const std::string& id,
                 Message place, Message what) {
        Problem problem;
        problem.severity = severity;
        problem.where = where;
        problem.id = id;
        problem.place = std::move(place);
        problem.what = std::move(what);
        problems_.push_back(std::move(problem));
        return problems_.back();
    }

    Problem& error(Problem::Where where, const std::string& id, Message place, Message what) {
        return add(Problem::Severity::Error, where, id, std::move(place), std::move(what));
    }

    Problem& warning(Problem::Where where, const std::string& id, Message place, Message what) {
        return add(Problem::Severity::Warning, where, id, std::move(place), std::move(what));
    }

    [[nodiscard]] std::string deviceName(const std::string& id) const {
        const model::Device* device = config_.findDevice(id);
        return device != nullptr ? device->displayName() : id;
    }

    [[nodiscard]] bool hasCommand(const std::string& deviceId, const std::string& command) const {
        const model::Device* device = config_.findDevice(deviceId);
        return device != nullptr && device->findCommand(command) != nullptr;
    }

    // --- Конфигурация целиком -------------------------------------------------------

    void checkConfig() {
        if (config_.devices.size() > kDeviceLimit) {
            error(Problem::Where::Config, {}, say("The whole configuration"),
                  say("%1 devices, the remote supports %2",
                      {std::to_string(config_.devices.size()), std::to_string(kDeviceLimit)}));
        }
        // Id — ссылки между устройствами и активностями: повтор делает их неоднозначными.
        std::set<std::string> ids;
        for (const auto& device : config_.devices) {
            if (!ids.insert(device.id).second) {
                error(Problem::Where::Device, device.id, say(device.displayName()),
                      say("another device or activity has the same id %1", {device.id}));
            }
        }
        for (const auto& activity : config_.activities) {
            if (!ids.insert(activity.id).second) {
                error(Problem::Where::Activity, activity.id, say(activity.label),
                      say("another device or activity has the same id %1", {activity.id}));
            }
        }

        // На пульте видны только имена: одинаковые путают.
        std::map<std::string, int> deviceNames;
        for (const auto& device : config_.devices) {
            ++deviceNames[lower(trimmed(device.displayName()))];
        }
        for (const auto& device : config_.devices) {
            const std::string name = trimmed(device.displayName());
            if (name.empty()) {
                warning(Problem::Where::Device, device.id, say("Device without a name"),
                        say("the device has no name, the remote shows an empty line"));
            } else if (deviceNames[lower(name)] > 1) {
                warning(Problem::Where::Device, device.id, say(name),
                        say("another device has the same name"))
                    .fix = Problem::Fix::Rename;
            }
        }
        std::map<std::string, int> activityNames;
        for (const auto& activity : config_.activities) {
            if (!activity.isPowerOff()) {
                ++activityNames[lower(trimmed(activity.label))];
            }
        }
        for (const auto& activity : config_.activities) {
            if (activity.isPowerOff()) {
                continue; // «кнопка Off» на экране пульта не показывается
            }
            const std::string name = trimmed(activity.label);
            if (name.empty()) {
                warning(Problem::Where::Activity, activity.id, say("Activity without a name"),
                        say("the activity has no name, the remote shows an empty line"));
            } else if (activityNames[lower(name)] > 1) {
                warning(Problem::Where::Activity, activity.id, say(name),
                        say("another activity has the same name"))
                    .fix = Problem::Fix::Rename;
            }
        }
    }

    // --- Устройство -----------------------------------------------------------------

    void checkDevice(const model::Device& device) {
        const std::string name = device.displayName();
        if (device.commands.empty()) {
            warning(Problem::Where::Device, device.id, say(name),
                    say("the device has no IR commands: its buttons do nothing"));
        }
        // Кнопки ссылаются на команду по имени: из двух одноимённых неясно, какая уйдёт.
        std::set<std::string> seen;
        std::set<std::string> reported;
        for (const auto& command : device.commands) {
            if (!seen.insert(command.name).second && reported.insert(command.name).second) {
                error(Problem::Where::Device, device.id, say("%1 · %2", {name, command.name}),
                      say("two commands are called “%1”, buttons cannot tell them apart",
                          {command.name}));
            }
        }

        for (const model::Button* button : hardButtons(device.controlGroups)) {
            if (isMacroAction(button->actionId)) {
                continue; // шаги макроса проверяет check()
            }
            const ActionRef action = splitAction(button->actionId);
            if (action.device != device.id) {
                error(
                    Problem::Where::Device, device.id, say("%1 · %2", {name, button->name}),
                    say("the key sends a command to another device — device keys work only for the "
                        "device itself"))
                    .hardKey = button->name;
                problems_.back().fix = Problem::Fix::FreeHardKey;
            } else if (!hasCommand(device.id, action.command)) {
                error(Problem::Where::Device, device.id, say("%1 · %2", {name, button->name}),
                      say("the key sends “%1”, the device has no such command", {action.command}))
                    .hardKey = button->name;
                problems_.back().fix = Problem::Fix::FreeHardKey;
            }
        }
        checkSoftButtons(softButtons(device.controlGroups), Problem::Where::Device, device.id,
                         name);

        // Питание и входы: команды, которые пульт шлёт, включая и переключая устройство.
        const pugi::xml_node node = detail::findById(root_, "Device", device.id);

        // Прошивка (State.lua) читает RelativeActions и только иначе — DiscreteActions:
        // при обоих прямой выбор (входа, «вкл»/«выкл») молча не используется.
        for (const pugi::xml_node state : node.child("States").children("State")) {
            if (state.child("RelativeActions") && state.child("DiscreteActions")) {
                const std::string stateName(detail::textOf(state.child("Id")));
                warning(Problem::Where::Device, device.id, say("%1 · %2", {name, stateName}),
                        say("the state has both step-by-step and direct actions: the remote uses only "
                            "the step-by-step ones, direct selection is ignored"));
            }
        }
        std::set<std::string> missing;
        for (const pugi::xml_node operation : operations(node.child("States"))) {
            if (detail::textOf(operation.child("Name")) != "SendCommand") {
                continue;
            }
            const std::string command = parameter(operation, "Command");
            const std::string target = parameter(operation, "DeviceId");
            if ((target == device.id || target.empty()) && !hasCommand(device.id, command) &&
                missing.insert(command).second) {
                error(Problem::Where::Device, device.id, say("%1 · power and inputs", {name}),
                      say("power or input switching sends “%1”, the device has no such command",
                          {command}));
            }
        }
    }

    /// Экранные кнопки: несуществующая команда, две кнопки на одном месте.
    void checkSoftButtons(const std::vector<const model::Button*>& buttons, Problem::Where where,
                          const std::string& id, const std::string& name) {
        std::map<int, int> positions;
        for (const model::Button* button : buttons) {
            const std::string label = button->label.empty() ? button->icon : button->label;
            Message place =
                label.empty() ? say("%1 · screen %2", {name, std::to_string(button->position + 1)})
                              : say("%1 · %2", {name, label});
            if (++positions[button->position] == 2) {
                Problem& problem = warning(where, id, place,
                                           say("two screen buttons at place %1, only one is shown",
                                               {std::to_string(button->position + 1)}));
                problem.softPosition = button->position;
                problem.fix = Problem::Fix::MoveSoftButton;
            }
            if (!button->icon.empty() && !isKnownButtonIcon(button->icon)) {
                Problem& problem = warning(where, id, place,
                                           say("the remote has no icon “%1”: the button is drawn blank",
                                               {button->icon}));
                problem.softPosition = button->position;
            }
            if (isMacroAction(button->actionId)) {
                continue; // шаги макроса проверяет check()
            }
            const ActionRef action = splitAction(button->actionId);
            if (config_.findDevice(action.device) == nullptr) {
                Problem& problem =
                    error(where, id, place, say("the screen button sends to a deleted device"));
                problem.softPosition = button->position;
                problem.fix = Problem::Fix::RemoveSoftButton;
            } else if (!hasCommand(action.device, action.command)) {
                Problem& problem = error(where, id, place,
                                         say("the screen button sends “%1”, %2 has no such command",
                                             {action.command, deviceName(action.device)}));
                problem.softPosition = button->position;
                problem.fix = Problem::Fix::RemoveSoftButton;
            }
        }
    }

    // --- Активность -----------------------------------------------------------------

    void checkActivity(const model::Activity& activity) {
        const std::string& name = activity.label;
        const std::string& id = activity.id;
        std::set<std::string> used; // устройства, которым шлют команды кнопки

        if (!activity.isPowerOff()) {
            const auto hard = hardButtons(activity.controlGroups);
            const auto soft = softButtons(activity.controlGroups);
            for (const model::Button* button : hard) {
                if (isMacroAction(button->actionId)) {
                    continue; // шаги макроса проверяет check()
                }
                const ActionRef action = splitAction(button->actionId);
                const Message place = say("%1 · %2", {name, button->name});
                if (config_.findDevice(action.device) == nullptr) {
                    Problem& problem = error(Problem::Where::Activity, id, place,
                                             say("the key sends to a deleted device"));
                    problem.hardKey = button->name;
                    problem.fix = Problem::Fix::FreeHardKey;
                    continue;
                }
                used.insert(action.device);
                if (!hasCommand(action.device, action.command)) {
                    Problem& problem = error(Problem::Where::Activity, id, place,
                                             say("the key sends “%1”, %2 has no such command",
                                                 {action.command, deviceName(action.device)}));
                    problem.hardKey = button->name;
                    problem.fix = Problem::Fix::FreeHardKey;
                }
            }
            for (const model::Button* button : soft) {
                const ActionRef action = splitAction(button->actionId);
                if (config_.findDevice(action.device) != nullptr) {
                    used.insert(action.device);
                }
            }
            checkSoftButtons(soft, Problem::Where::Activity, id, name);
            if (hard.empty() && soft.empty()) {
                warning(Problem::Where::Activity, id, say(name),
                        say("the activity has no buttons: it only switches devices"));
            }
        }

        checkStartSteps(activity, name);

        // Питание: кого включает и выключает.
        std::set<std::string> powered;
        std::set<std::string> listed;
        bool reportedDeleted = false;
        auto deleted = [&](const std::string& device, Message what) {
            if (config_.findDevice(device) == nullptr) {
                if (!reportedDeleted) {
                    error(Problem::Where::Activity, id, say("%1 · power", {name}), std::move(what))
                        .fix = Problem::Fix::RemoveDeleted;
                    reportedDeleted = true;
                }
                return true;
            }
            return false;
        };
        for (const auto& device : activity.powerOn) {
            listed.insert(device);
            if (!deleted(device, say("switches on a device that was deleted"))) {
                powered.insert(device);
            }
        }
        for (const auto& device : activity.powerOff) {
            listed.insert(device);
            deleted(device, say("switches off a device that was deleted"));
        }

        // Роли: DEFAULT есть в каждой активности, роль — у одного устройства.
        if (!activity.isPowerOff()) {
            std::set<std::string> roles;
            for (const auto& role : activity.roles) {
                if (!roles.insert(role.name).second) {
                    error(Problem::Where::Activity, id, say("%1 · %2", {name, role.name}),
                          say("the role is given to more than one device"));
                }
                if (config_.findDevice(role.deviceId) == nullptr) {
                    error(Problem::Where::Activity, id, say("%1 · %2", {name, role.name}),
                          say("the role belongs to a device that was deleted"))
                        .fix = Problem::Fix::RemoveDeleted;
                }
            }
            if (!roles.contains("DEFAULT")) {
                warning(Problem::Where::Activity, id, say("%1 · DEFAULT", {name}),
                        say("no device has the main role (DEFAULT): keys without an assignment do "
                            "nothing"));
            }
        }

        // Каждое устройство: включено или выключено (иначе пульт не знает, что с ним делать).
        std::vector<std::string> unset;
        for (const auto& device : config_.devices) {
            if (!listed.contains(device.id)) {
                unset.push_back(device.displayName());
            }
        }
        if (!unset.empty()) {
            std::string list;
            for (const auto& device : unset) {
                list += (list.empty() ? "" : ", ") + device;
            }
            warning(Problem::Where::Activity, id, say("%1 · power", {name}),
                    say("not set whether these devices are on or off: %1", {list}))
                .fix = Problem::Fix::CompletePowerMap;
        }

        // Кнопки шлют устройству, которое активность не включает.
        for (const auto& [device, input] : activity.inputs) {
            const model::Device* target = config_.findDevice(device);
            if (target != nullptr && !target->inputs.empty() &&
                std::ranges::find(target->inputs, input) == target->inputs.end()) {
                warning(Problem::Where::Activity, id, say("%1 · inputs", {name}),
                        say("switches %1 to input “%2”, which the device does not have",
                            {target->displayName(), input}));
            }
            if (target != nullptr) {
                used.insert(device);
            }
        }
        for (const auto& device : used) {
            const model::Device* target = config_.findDevice(device);
            if (target != nullptr && target->powerControlled && !powered.contains(device)) {
                Problem& problem =
                    warning(Problem::Where::Activity, id, say("%1 · power", {name}),
                            say("%1 is used by the activity but not switched on by it",
                                {target->displayName()}));
                problem.fix = Problem::Fix::PowerOnDevice;
                problem.fixDevice = device;
            }
        }
    }

    /// Шаги запуска: команды несуществующим устройствам и командам.
    void checkStartSteps(const model::Activity& activity, const std::string& name) {
        const pugi::xml_node node = detail::findById(root_, "Activity", activity.id);
        bool reported = false;
        for (const pugi::xml_node action : node.child("EnterActions").children("Action")) {
            const pugi::xml_node operation = action.child("Operation");
            const std::string device = parameter(operation, "DeviceId");
            if (device.empty() || reported) {
                continue;
            }
            const std::string_view kind = detail::textOf(operation.child("Name"));
            if (config_.findDevice(device) == nullptr) {
                error(Problem::Where::Activity, activity.id, say("%1 · start", {name}),
                      say("a start step is addressed to a device that was deleted"))
                    .fix = Problem::Fix::RemoveBrokenSteps;
                reported = true;
            } else if (kind == "SendCommand" &&
                       !hasCommand(device, parameter(operation, "Command"))) {
                error(Problem::Where::Activity, activity.id, say("%1 · start", {name}),
                      say("a start step sends “%1”, %2 has no such command",
                          {parameter(operation, "Command"), deviceName(device)}))
                    .fix = Problem::Fix::RemoveBrokenSteps;
                reported = true;
            }
        }
    }

    const model::RemoteConfig& config_;
    pugi::xml_node root_;
    std::vector<Problem> problems_;
};

/// Узел владельца кнопок проблемы.
pugi::xml_node ownerNode(pugi::xml_node root, const Problem& problem) {
    return detail::findById(root, problem.where == Problem::Where::Device ? "Device" : "Activity",
                            problem.id);
}

/// Экранные кнопки владельца в дереве (все группы, кроме аппаратных).
std::vector<pugi::xml_node> softNodes(pugi::xml_node owner) {
    std::vector<pugi::xml_node> out;
    for (pugi::xml_node group : owner.child("Presentation").children("ControlGroup")) {
        if (std::string_view(group.attribute("name").value()) != kHardGroup) {
            for (pugi::xml_node button : group.children("Button")) {
                out.push_back(button);
            }
        }
    }
    return out;
}

/// Убирает группу кнопок, если в ней не осталось кнопок.
void dropIfEmpty(pugi::xml_node group) {
    if (group && !group.child("Button")) {
        group.parent().remove_child(group);
    }
}

} // namespace

std::size_t countProblems(const std::vector<Problem>& problems, Problem::Severity severity) {
    return static_cast<std::size_t>(std::ranges::count_if(
        problems, [severity](const Problem& p) { return p.severity == severity; }));
}

std::vector<Problem> ConfigDocument::check() const {
    pugi::xml_document dom;
    if (!detail::loadDom(dom, impl_->xml)) {
        return {}; // документ открылся, значит XML разбирается; сюда не попадаем
    }
    auto problems = Checker(impl_->model, dom.child("Root")).run();
    for (const auto& device : impl_->model.devices) {
        for (const auto& command : device.commands) {
            auto details = commandDetails(device.id, command.name);
            if (!details || details->signal.durations.empty()) {
                Problem problem;
                problem.severity = Problem::Severity::Error;
                problem.where = Problem::Where::Device;
                problem.id = device.id;
                problem.place = say("%1 · %2", {device.displayName(), command.name});
                problem.what = details ? say("the command produces an empty IR signal") : details.error();
                problems.push_back(std::move(problem));
            } else if (details->learned && details->decoded &&
                       (details->decoded->protocol.starts_with("RC5") || details->decoded->protocol.starts_with("RC6"))) {
                // Записанный сигнал — с одним значением бита переключения: два нажатия
                // подряд устройство примет за удержание.
                Problem problem;
                problem.severity = Problem::Severity::Warning;
                problem.where = Problem::Where::Device;
                problem.id = device.id;
                problem.place = say("%1 · %2", {device.displayName(), command.name});
                problem.what = say("%1 is stored as a recorded signal: its toggle bit never changes, so the "
                                   "device may take two presses in a row for one long press",
                                   {details->decoded->protocol});
                problems.push_back(std::move(problem));
            }
        }
    }
    // Кнопки-макросы: шаги шлют существующие команды существующим устройствам.
    auto checkMacros = [&](Problem::Where where, const std::string& ownerId, const std::string& ownerName,
                           const std::vector<model::ControlGroup>& groups) {
        for (const auto& group : groups) {
            for (const auto& button : group.buttons) {
                if (!isMacroAction(button.actionId)) {
                    continue;
                }
                const std::string title = !button.name.empty() ? button.name : button.label;
                Problem problem;
                problem.severity = Problem::Severity::Error;
                problem.where = where;
                problem.id = ownerId;
                problem.place = say("%1 · %2", {ownerName, title});
                if (!button.name.empty()) {
                    problem.hardKey = button.name;
                    problem.fix = Problem::Fix::FreeHardKey;
                }
                const auto steps = macro(button.actionId);
                if (!steps) {
                    problem.what = say("the button runs a macro that is missing from ActionLists.xml");
                    problems.push_back(problem);
                    continue;
                }
                for (const MacroStep& step : *steps) {
                    const model::Device* device = impl_->model.findDevice(step.deviceId);
                    if (device == nullptr) {
                        problem.what = say("a macro step is addressed to a device that was deleted");
                    } else if (step.step.kind == StateStep::Kind::Command &&
                               device->findCommand(step.step.command) == nullptr) {
                        problem.what = say("a macro step sends “%1”, %2 has no such command",
                                           {step.step.command, device->displayName()});
                    } else {
                        continue;
                    }
                    problems.push_back(problem);
                    break;
                }
            }
        }
    };
    for (const auto& device : impl_->model.devices) {
        checkMacros(Problem::Where::Device, device.id, device.displayName(), device.controlGroups);
    }
    for (const auto& activity : impl_->model.activities) {
        checkMacros(Problem::Where::Activity, activity.id, activity.label, activity.controlGroups);
    }

    for (const std::string& id : devicesOutsideRf()) {
        const model::Device* device = impl_->model.findDevice(id);
        Problem problem;
        problem.severity = Problem::Severity::Warning;
        problem.where = Problem::Where::Device;
        problem.id = id;
        problem.place = say(device != nullptr ? device->displayName() : id);
        problem.what = say("the remote has an RF blaster, but this device is not assigned to it: commands go "
                           "from the remote's front IR. Assign it on the remote: Options → RF Receiver Settings");
        problems.push_back(std::move(problem));
    }
    std::ranges::stable_sort(problems, [](const Problem& a, const Problem& b) {
        return a.severity == Problem::Severity::Error && b.severity != Problem::Severity::Error;
    });
    return problems;
}

Result<void> ConfigDocument::fixProblem(const Problem& problem) {
    const model::RemoteConfig& config = impl_->model;
    auto commandExists = [&config](const std::string& actionId) {
        const ActionRef action = splitAction(actionId);
        const model::Device* device = config.findDevice(action.device);
        return device != nullptr && device->findCommand(action.command) != nullptr;
    };
    const ButtonOwner owner = problem.where == Problem::Where::Device
                                  ? ButtonOwner::device(problem.id)
                                  : ButtonOwner::activity(problem.id);
    switch (problem.fix) {
    case Problem::Fix::None:
        return makeError(ErrorCode::Unsupported, "this problem has to be fixed by hand");

    case Problem::Fix::FreeHardKey:
        return setHardButton(owner, problem.hardKey, {}, {});

    case Problem::Fix::RemoveSoftButton:
        return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
            // Последняя кнопка на этом месте, команда которой не существует.
            const auto buttons = softNodes(ownerNode(root, problem));
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                if (it->child("Position").text().as_int(-1) == problem.softPosition &&
                    !commandExists(std::string(detail::textOf(it->child("ActionId"))))) {
                    pugi::xml_node group = it->parent();
                    group.remove_child(*it);
                    dropIfEmpty(group);
                    return {};
                }
            }
            return makeError(ErrorCode::NotFound, "the problem is already gone");
        });

    case Problem::Fix::MoveSoftButton: {
        const int free = freeSoftPosition(owner);
        return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
            // Вторая кнопка на этом месте — на первое свободное.
            int seen = 0;
            for (pugi::xml_node button : softNodes(ownerNode(root, problem))) {
                if (button.child("Position").text().as_int(-1) == problem.softPosition &&
                    ++seen == 2) {
                    button.child("Position").text() = free;
                    return {};
                }
            }
            return makeError(ErrorCode::NotFound, "the problem is already gone");
        });
    }

    case Problem::Fix::RemoveBrokenSteps:
        return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
            pugi::xml_node enter = ownerNode(root, problem).child("EnterActions");
            int removed = 0;
            for (pugi::xml_node action = enter.child("Action"); action;) {
                const pugi::xml_node next = action.next_sibling("Action");
                const pugi::xml_node operation = action.child("Operation");
                const std::string device = parameter(operation, "DeviceId");
                const model::Device* target = config.findDevice(device);
                const bool broken =
                    !device.empty() &&
                    (target == nullptr ||
                     (detail::textOf(operation.child("Name")) == "SendCommand" &&
                      target->findCommand(parameter(operation, "Command")) == nullptr));
                if (broken) {
                    enter.remove_child(action);
                    ++removed;
                }
                action = next;
            }
            if (removed == 0) {
                return makeError(ErrorCode::NotFound, "the problem is already gone");
            }
            return {};
        });

    case Problem::Fix::RemoveDeleted:
        return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
            pugi::xml_node activity = ownerNode(root, problem);
            int removed = 0;
            auto removeWhere = [&](pugi::xml_node parent, const char* tag, auto deviceOf) {
                for (pugi::xml_node node = parent.child(tag); node;) {
                    const pugi::xml_node next = node.next_sibling(tag);
                    if (config.findDevice(deviceOf(node)) == nullptr) {
                        parent.remove_child(node);
                        ++removed;
                    }
                    node = next;
                }
            };
            auto text = [](pugi::xml_node node) { return std::string(detail::textOf(node)); };
            removeWhere(activity.child("Power"), "On", text);
            removeWhere(activity.child("Power"), "Off", text);
            removeWhere(activity, "Role", [](pugi::xml_node role) {
                return std::string(detail::textOf(role.child("DeviceId")));
            });
            if (removed == 0) {
                return makeError(ErrorCode::NotFound, "the problem is already gone");
            }
            return {};
        });

    case Problem::Fix::CompletePowerMap:
        return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
            pugi::xml_node activity = ownerNode(root, problem);
            const model::Activity* current = config.findActivity(problem.id);
            if (!activity || current == nullptr) {
                return makeError(ErrorCode::NotFound, "no activity %1", {problem.id});
            }
            pugi::xml_node power =
                activity.child("Power") ? activity.child("Power") : activity.append_child("Power");
            for (const auto& device : config.devices) {
                if (std::ranges::find(current->powerOn, device.id) == current->powerOn.end() &&
                    std::ranges::find(current->powerOff, device.id) == current->powerOff.end()) {
                    detail::appendText(power, "Off", device.id);
                }
            }
            return {};
        });

    case Problem::Fix::PowerOnDevice: {
        const model::Activity* activity = config.findActivity(problem.id);
        if (activity == nullptr) {
            return makeError(ErrorCode::NotFound, "no activity %1", {problem.id});
        }
        ActivityDraft draft = draftOf(*activity);
        if (std::ranges::find(draft.devices, problem.fixDevice) == draft.devices.end()) {
            draft.devices.push_back(problem.fixDevice);
        }
        return updateActivity(problem.id, draft, false);
    }

    case Problem::Fix::Rename: {
        if (problem.where == Problem::Where::Device) {
            const model::Device* device = config.findDevice(problem.id);
            if (device == nullptr) {
                return makeError(ErrorCode::NotFound, "no device %1", {problem.id});
            }
            return setDeviceLabel(problem.id,
                                  detail::uniqueLabel(config, trimmed(device->displayName())));
        }
        const model::Activity* activity = config.findActivity(problem.id);
        if (activity == nullptr) {
            return makeError(ErrorCode::NotFound, "no activity %1", {problem.id});
        }
        const std::string base = trimmed(activity->label);
        auto taken = [&](const std::string& candidate) {
            return std::ranges::any_of(config.activities, [&](const model::Activity& other) {
                return other.id != activity->id && lower(trimmed(other.label)) == lower(candidate);
            });
        };
        std::string label = base;
        for (int n = 2; taken(label); ++n) {
            label = base + " (" + std::to_string(n) + ")";
        }
        return setActivityLabel(problem.id, label);
    }
    }
    return makeError(ErrorCode::Unsupported, "this problem has to be fixed by hand");
}

Result<std::size_t> ConfigDocument::fixAll() {
    // Исправление одной проблемы меняет другие, поэтому после каждого — проверка заново.
    // Не получившиеся запоминаются, чтобы не повторять их бесконечно.
    std::size_t fixed = 0;
    std::set<std::string> failed;
    std::set<std::string> attempted;
    auto key = [](const Problem& p) {
        return std::to_string(static_cast<int>(p.fix)) + '|' + p.id + '|' + p.hardKey + '|' +
               std::to_string(p.softPosition) + '|' + p.fixDevice + '|' + p.what.message;
    };
    for (int guard = 0; guard < 1000; ++guard) {
        const auto problems = check();
        const auto next = std::ranges::find_if(
            problems, [&](const Problem& p) { return p.fixable() && !failed.contains(key(p)); });
        if (next == problems.end()) {
            break;
        }
        const std::string id = key(*next);
        if (!attempted.insert(id).second) {
            failed.insert(id); // «исправили», а проблема осталась — больше не трогаем
            continue;
        }
        if (fixProblem(*next)) {
            ++fixed;
        } else {
            failed.insert(id);
        }
    }
    return fixed;
}

} // namespace harmony
