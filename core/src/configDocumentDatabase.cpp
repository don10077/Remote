/**
 * @file configDocumentDatabase.cpp
 * @brief Устройство из базы ИК-кодов Logitech → устройство конфигурации.
 *
 * Как у программы Logitech: команды, кнопки по типам команд, описание
 * питания (State Power) и входов (State Input). Команды — протоколом в
 * IrProto.bin (ProtocolLibrary::compile): пульт сам шлёт кадры повтора и
 * переключает бит-переключатель. Что так не записать — сырым сигналом в SsIr.bin.
 */

#include "configDocumentImpl.h"
#include "harmony/core/ir/nativeProtocol.h"
#include "harmony/core/irdb/irDatabase.h"

#include <algorithm>
#include <optional>
#include <set>

namespace harmony {

namespace {

using detail::appendText;

/// Аппаратная кнопка пульта → типы команд базы по убыванию предпочтения.
const std::vector<std::pair<const char*, std::vector<const char*>>>& hardKeyTypes() {
    static const std::vector<std::pair<const char*, std::vector<const char*>>> keys = {
        {"Menu", {"Menu", "Setup"}},
        {"Exit", {"Exit", "Return", "Cancel"}},
        {"UpArrow", {"PageUp"}},
        {"DownArrow", {"PageDown"}},
        {"Info", {"Info", "Display"}},
        {"Guide", {"Guide", "TopMenu"}},
        {"Red", {"Red"}},
        {"Green", {"Green"}},
        {"Yellow", {"Yellow"}},
        {"Blue", {"Blue"}},
        {"VolumeUp", {"VolumeUp"}},
        {"VolumeDown", {"VolumeDown"}},
        {"DirectionLeft", {"DirectionLeft"}},
        {"DirectionUp", {"DirectionUp"}},
        {"DirectionRight", {"DirectionRight"}},
        {"DirectionDown", {"DirectionDown"}},
        {"Select", {"Select"}},
        {"ChannelUp", {"ChannelUp", "NextPreset"}},
        {"ChannelDown", {"ChannelDown", "PrevPreset"}},
        {"VolumeMute", {"Mute"}},
        {"PrevChannel", {"PrevChannel"}},
        {"Rewind", {"Rewind"}},
        {"SkipBack", {"SkipBackward"}},
        {"Record", {"Record"}},
        {"Play", {"Play"}},
        {"Pause", {"Pause"}},
        {"FastForward", {"FastForward"}},
        {"SkipForward", {"SkipForward"}},
        {"Stop", {"Stop"}},
        {"Number1", {"Number1"}},
        {"Number2", {"Number2"}},
        {"Number3", {"Number3"}},
        {"Number4", {"Number4"}},
        {"Number5", {"Number5"}},
        {"Number6", {"Number6"}},
        {"Number7", {"Number7"}},
        {"Number8", {"Number8"}},
        {"Number9", {"Number9"}},
        {"NumberPlus", {"Star", "Greater10"}},
        {"Number0", {"Number0"}},
        {"NumberEnter", {"NumberEnter"}},
    };
    return keys;
}

constexpr std::size_t kMaxSoftButtons = 96; ///< Длинный список на маленьком экране бесполезен

/// Типы устройств, которые показывают картинку (свойство IsDisplayDevice).
bool isDisplay(std::string_view type) {
    return type == "Television" || type == "Projector" || type == "Monitor" || type == "TvDvd" ||
           type == "TvDvdVcr" || type == "TvHdd" || type == "TvVcr";
}

/// Можно ли выполнить шаги: все команды перенесены, состояния — из тех, что описываются.
bool usable(const std::vector<irdb::DbAction>& steps, const std::set<std::string>& commands,
            const std::set<std::string>& states) {
    if (steps.empty()) {
        return false;
    }
    return std::ranges::all_of(steps, [&](const irdb::DbAction& step) {
        switch (step.kind) {
        case irdb::DbAction::Kind::Press:
            return commands.contains(step.command);
        case irdb::DbAction::Kind::Delay:
            return true;
        case irdb::DbAction::Kind::State:
            return states.contains(step.stateName);
        }
        return false;
    });
}

/// Шаги базы → действия пульта (SendCommand, SendDelay, SetValue).
void appendActions(pugi::xml_node parent, const std::string& deviceId,
                   const std::vector<irdb::DbAction>& steps) {
    for (const irdb::DbAction& step : steps) {
        switch (step.kind) {
        case irdb::DbAction::Kind::Press:
            detail::appendSendCommand(parent, deviceId, step.command);
            break;
        case irdb::DbAction::Kind::Delay: {
            pugi::xml_node action = parent.append_child("Action");
            appendText(action, "Target", "Device");
            pugi::xml_node operation = action.append_child("Operation");
            appendText(operation, "Name", "SendDelay");
            appendText(operation, "Parameter", deviceId).append_attribute("name") = "DeviceId";
            appendText(operation, "Parameter", std::to_string(std::max(0, step.delayMs)))
                .append_attribute("name") = "Delay";
            break;
        }
        case irdb::DbAction::Kind::State:
            detail::appendSetValue(parent, deviceId, step.stateName, step.stateValue);
            break;
        }
    }
}

/// Состояние с перечислением значений; States создаётся перед Commands.
pugi::xml_node appendState(pugi::xml_node device, const std::string& id,
                           const std::vector<std::string>& values, int delayMs) {
    pugi::xml_node states = device.child("States");
    if (!states) {
        states = device.insert_child_before("States", device.child("Commands"));
    }
    pugi::xml_node state = states.append_child("State");
    appendText(state, "Id", id);
    for (const std::string& value : values) {
        appendText(state, "Value", value);
    }
    appendText(state, "Delay", std::to_string(std::max(0, delayMs)));
    return state;
}

} // namespace

Result<DbImportReport> ConfigDocument::importDbDevice(const irdb::DbDevice& source,
                                                      const irdb::ProtocolLibrary& protocols) {
    DbImportReport report;

    // Команды — заранее: без них переносить нечего. Протоколом (IrProto.bin), как у
    // Logitech, если получилось; иначе сигналом (SsIr.bin).
    struct Converted {
        const irdb::DbCommand* command;
        formats::Bytes entry;                    ///< Сырой сигнал (запасной путь)
        std::optional<ir::NativeCommand> native; ///< Протокол и код
    };
    std::vector<Converted> converted;
    std::set<std::string> names;
    for (const irdb::DbCommand& command : source.commands) {
        if (!names.insert(command.name).second) {
            continue; // одинаковые имена: берётся первая команда
        }
        auto signal = protocols.render(command);
        if (!signal) {
            report.skipped.push_back(command.name + ": " + signal.error().message);
            continue;
        }
        auto native = protocols.compile(command);
        converted.push_back({&command, ir::encodeRawEntry(*signal),
                             native ? std::optional(std::move(*native)) : std::nullopt});
    }
    if (converted.empty()) {
        return makeError(ErrorCode::Unsupported,
                         "none of the device's commands could be converted");
    }
    std::set<std::string> available;
    for (const Converted& item : converted) {
        available.insert(item.command->name);
    }

    const std::string type = irdb::deviceTypeName(source.deviceType);
    const std::string id = detail::uniqueId(impl_->model);
    const DeviceDraft draft{
        .label = detail::uniqueLabel(impl_->model, source.manufacturer + " " + source.model),
        .type = type == "Unknown" ? "SetTopBox" : type,
        .manufacturer = source.manufacturer,
        .model = source.model};

    // Питание: отдельные команды → Off/On; переключатель (или одинаковые) → «следующее».
    const bool samePowerCommand = source.powerOn == source.powerOff;
    const auto& toggleSteps = source.powerToggle.empty() ? source.powerOn : source.powerToggle;
    const bool discretePower = source.powerType == 1 && !samePowerCommand &&
                               usable(source.powerOn, available, {}) &&
                               usable(source.powerOff, available, {});
    const bool togglePower = !discretePower && (source.powerType == 1 || source.powerType == 2) &&
                             usable(toggleSteps, available, {});
    // Входы: прямой выбор, если у каждого входа есть свои действия; иначе перебор.
    const std::set<std::string> powerState = {"Power"};
    const bool discreteInputs =
        !source.inputs.empty() && std::ranges::all_of(source.inputs, [&](const auto& input) {
            return usable(input.actions, available, powerState);
        });
    const bool relativeInputs = !discreteInputs && source.inputs.size() > 1 &&
                                usable(source.inputNext, available, powerState);

    // Кнопки: аппаратные — по типам команд, остальные команды — на экран (питание и входы первыми).
    std::vector<std::pair<const char*, const irdb::DbCommand*>> hardButtons;
    std::set<const irdb::DbCommand*> onHardKey;
    for (const auto& [key, types] : hardKeyTypes()) {
        const irdb::DbCommand* found = nullptr;
        for (const char* wanted : types) {
            for (const Converted& item : converted) {
                if (item.command->type == wanted && !onHardKey.contains(item.command)) {
                    found = item.command;
                    break;
                }
            }
            if (found != nullptr) {
                break;
            }
        }
        if (found != nullptr) {
            hardButtons.emplace_back(key, found);
            onHardKey.insert(found);
        }
    }
    auto rank = [](const irdb::DbCommand* command) {
        if (command->type.starts_with("Power") || command->name.starts_with("Power")) {
            return 0;
        }
        if (command->type.starts_with("Input") || command->name.starts_with("Input")) {
            return 1;
        }
        return 2;
    };
    std::vector<const irdb::DbCommand*> softButtons;
    for (const Converted& item : converted) {
        if (!onHardKey.contains(item.command)) {
            softButtons.push_back(item.command);
        }
    }
    std::ranges::stable_sort(softButtons, {}, rank);
    if (softButtons.size() > kMaxSoftButtons) {
        softButtons.resize(kMaxSoftButtons);
    }

    // Всё — одной правкой: дерево разбирается один раз.
    auto added =
        detail::mutate(*impl_, [&](pugi::xml_node root, detail::IrData& ir) -> Result<void> {
            pugi::xml_node device = detail::appendDevice(root, id, draft);
            if (isDisplay(draft.type)) {
                detail::appendProperty(device.child("Properties"), "IsDisplayDevice", "true");
            }

            // Команды: протоколом в IrProto.bin; не вышло или таблица полна — сигналом в SsIr.bin.
            pugi::xml_node commands = device.child("Commands");
            bool protocolsChanged = false;
            for (const Converted& item : converted) {
                pugi::xml_node command = commands.append_child("Command");
                appendText(command, "Name", item.command->name);
                pugi::xml_node data = command.append_child("Data");
                if (item.native) {
                    auto protocolTable = detail::writableTable(ir.irProto, formats::kIrProtoFileName);
                    if (protocolTable) {
                        formats::IrTable& table = **protocolTable;
                        const std::size_t before = table.entries.size();
                        const std::size_t index = table.intern(item.native->protocol);
                        if (index <= detail::kMaxProtocolIndex && formats::fitsOffsets(table)) {
                            appendText(data, "Protocol", std::to_string(index));
                            appendText(data, "Code", ir::codeText(item.native->code, static_cast<unsigned>(index)));
                            protocolsChanged = protocolsChanged || table.entries.size() != before;
                            ++report.protocolCommands;
                            continue;
                        }
                        if (table.entries.size() != before) {
                            table.entries.pop_back(); // не поместилась — не оставлять
                        }
                    }
                }
                auto rawTable = detail::writableTable(ir.ssIr, formats::kSsIrFileName);
                if (!rawTable) {
                    return std::unexpected(std::move(rawTable.error()));
                }
                const std::size_t index = (*rawTable)->intern(item.entry);
                if (index > detail::kMaxRawIndex || !formats::fitsOffsets(**rawTable)) {
                    return makeError(ErrorCode::Format, "no room for a new code in SsIr.bin");
                }
                appendText(data, "Protocol", "-1");
                appendText(data, "Code", detail::rawCode(index));
            }
            if (protocolsChanged) {
                if (auto hashed = detail::updateProtocolHash(root, ir.irProto); !hashed) {
                    return hashed;
                }
            }

            if (discretePower || togglePower) {
                pugi::xml_node state =
                    appendState(device, "Power", {"Off", "On"}, source.powerOnDelayMs);
                if (discretePower) {
                    pugi::xml_node actions = state.append_child("DiscreteActions");
                    pugi::xml_node off = actions.append_child("SetAction");
                    appendActions(off, id, source.powerOff);
                    appendText(off, "Name", "Off");
                    pugi::xml_node on = actions.append_child("SetAction");
                    appendActions(on, id, source.powerOn);
                    appendText(on, "Name", "On");
                } else {
                    appendActions(state.append_child("RelativeActions").append_child("NextAction"),
                                  id, toggleSteps);
                }
                detail::enablePowerOff(root, device, id);
            }
            for (const auto& [key, command] : hardButtons) {
                detail::appendHardButton(device, key, id, command->name);
            }
            for (std::size_t i = 0; i < softButtons.size(); ++i) {
                detail::appendSoftButton(device, static_cast<int>(i), softButtons[i]->name, id,
                                         softButtons[i]->name);
            }
            if (discreteInputs || relativeInputs) {
                std::vector<std::string> values;
                for (const auto& input : source.inputs) {
                    values.push_back(input.name);
                }
                pugi::xml_node state = appendState(device, "Input", values, source.inputDelayMs);
                if (discreteInputs) {
                    pugi::xml_node actions = state.append_child("DiscreteActions");
                    for (const auto& input : source.inputs) {
                        pugi::xml_node set = actions.append_child("SetAction");
                        appendActions(set, id, input.actions);
                        appendText(set, "Name", input.name);
                    }
                } else {
                    pugi::xml_node actions = state.append_child("RelativeActions");
                    appendActions(actions.append_child("NextAction"), id, source.inputNext);
                    if (usable(source.inputPrev, available, powerState)) {
                        appendActions(actions.append_child("PrevAction"), id, source.inputPrev);
                    }
                }
            }
            return {};
        });
    if (!added) {
        return std::unexpected(std::move(added.error()));
    }
    report.deviceId = id;
    report.commands = static_cast<int>(converted.size());
    report.power = discretePower || togglePower;
    report.inputs = discreteInputs || relativeInputs;
    report.hardButtons = static_cast<int>(hardButtons.size());
    report.softButtons = static_cast<int>(softButtons.size());

    return report;
}

} // namespace harmony
