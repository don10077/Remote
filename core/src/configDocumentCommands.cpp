/**
 * @file configDocumentCommands.cpp
 * @brief Команды устройства: новое устройство, выученные команды, описание питания.
 *
 * Команда с выученным сигналом:
 * @code
 *   <Command><Name>PowerOn</Name><Data><Protocol>-1</Protocol><Code>0xFFFF0300</Code></Data></Command>
 * @endcode
 * Протокол -1 — «сырой» код, третий байт кода — номер записи в SsIr.bin.
 *
 * Питание (State Power): при отдельных командах — DiscreteActions с SetAction
 * «Off»/«On», при одной кнопке — RelativeActions/NextAction с PowerToggle.
 */

#include "configDocumentImpl.h"
#include "harmony/core/ir/carrierVm.h"
#include "harmony/core/ir/irEncode.h"
#include "harmony/core/ir/irProtocol.h"
#include "harmony/core/ir/nativeProtocol.h"
#include "harmony/core/macro.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <random>
#include <span>

namespace harmony {

namespace {

using detail::appendText;

/// Есть ли у устройства команда с таким именем (в дереве).
bool hasCommand(pugi::xml_node device, std::string_view name) {
    for (pugi::xml_node command : device.child("Commands").children("Command")) {
        if (detail::textOf(command.child("Name")) == name) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Описывает питание устройства, если оно ещё не описано и уже есть нужные команды.
 *
 * Существующее описание не трогается: у перенесённых устройств оно настроено Logitech.
 */
void describePower(pugi::xml_node root, pugi::xml_node device, const std::string& deviceId) {
    pugi::xml_node states = device.child("States");
    for (pugi::xml_node state : states.children("State")) {
        if (detail::textOf(state.child("Id")) == "Power") {
            return;
        }
    }
    const bool discrete = hasCommand(device, "PowerOn") && hasCommand(device, "PowerOff");
    const bool toggle = hasCommand(device, "PowerToggle");
    if (!discrete && !toggle) {
        return;
    }
    if (!states) {
        pugi::xml_node before = device.child("Numeric") ? device.child("Numeric") : device.child("Commands");
        states = before ? device.insert_child_before("States", before) : device.append_child("States");
    }
    pugi::xml_node state = states.prepend_child("State");
    appendText(state, "Id", "Power");
    appendText(state, "Value", "Off");
    appendText(state, "Value", "On");
    // Сколько ждать после включения, прежде чем слать остальное: телевизорам нужно дольше.
    const std::string_view type = detail::textOf(device.child("Type"));
    appendText(state, "Delay", type == "Television" || type == "Projector" ? "8000" : "3000");
    if (discrete) {
        pugi::xml_node actions = state.append_child("DiscreteActions");
        for (const auto& [value, command] : {std::pair{"Off", "PowerOff"}, std::pair{"On", "PowerOn"}}) {
            pugi::xml_node set = actions.append_child("SetAction");
            detail::appendSendCommand(set, deviceId, command);
            appendText(set, "Name", value);
        }
    } else {
        detail::appendSendCommand(state.append_child("RelativeActions").append_child("NextAction"), deviceId, "PowerToggle");
    }

    // Теперь пульт управляет питанием сам; «выключить всё» выключает и это устройство.
    detail::enablePowerOff(root, device, deviceId);
}

/**
 * @brief Понимает ли запись простой разбор (ir::renderProtocol).
 *
 * Он читает символ как два слова и знает только символы 0 и 1. У записей,
 * собранных из базы (символы шире двух слов, шестнадцатеричные алфавиты),
 * он дал бы похожий, но неверный сигнал — их надо исполнять, как пульт.
 */
bool simpleLayout(std::span<const std::uint8_t> entry) {
    if (entry.size() < 7 || entry[0] != 0x01) {
        return false;
    }
    const std::size_t count = entry[6];
    if (entry.size() < 7 + count * 2) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = entry[7 + i * 2] | (static_cast<std::size_t>(entry[8 + i * 2]) << 8);
        if (at + 16 > entry.size()) {
            return false;
        }
        const bool hasData = (entry[at + 10] | entry[at + 11]) != 0;
        if (hasData && (entry[at + 8] > 2 || entry[at + 9] != 2)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Записывает команду протоколом в IrProto.bin (или заменяет одноимённую).
 * @return false — таблица протоколов не читается или в ней нет места; документ не изменён,
 *         команду надо сохранять сигналом.
 */
bool storeNative(ConfigDocument::Impl& impl, const std::string& deviceId, const std::string& name,
                 const ir::NativeCommand& native) {
    auto stored = detail::mutate(impl, [&](pugi::xml_node root, detail::IrData& ir) -> Result<void> {
        auto table = detail::writableTable(ir.irProto, formats::kIrProtoFileName);
        if (!table) {
            return std::unexpected(std::move(table.error()));
        }
        const std::size_t index = (*table)->intern(native.protocol);
        if (index > detail::kMaxProtocolIndex || !formats::fitsOffsets(**table)) {
            return makeError(ErrorCode::Format, "no room for a new protocol in IrProto.bin");
        }
        pugi::xml_node device = detail::findById(root, "Device", deviceId);
        pugi::xml_node data = detail::commandWithoutData(device, name).append_child("Data");
        appendText(data, "Protocol", std::to_string(index));
        appendText(data, "Code", ir::codeText(native.code, static_cast<unsigned>(index)));
        if (auto hashed = detail::updateProtocolHash(root, ir.irProto); !hashed) {
            return hashed;
        }
        if (name == "PowerOn" || name == "PowerOff" || name == "PowerToggle") {
            describePower(root, device, deviceId);
        }
        return {};
    });
    return stored.has_value();
}

} // namespace

Result<std::string> ConfigDocument::createDevice(const DeviceDraft& draft) {
    if (draft.label.empty() || draft.type.empty()) {
        return makeError(ErrorCode::Format, "the device must have a name and a type");
    }
    const std::string id = detail::uniqueId(impl_->model);
    auto added = detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        detail::appendDevice(root, id, draft);
        return {};
    });
    if (!added) {
        return std::unexpected(std::move(added.error()));
    }
    return id;
}

Result<void> ConfigDocument::learnCommand(const std::string& deviceId, const std::string& name,
                                          const ir::IrSignal& signal) {
    if (name.empty()) {
        return makeError(ErrorCode::Format, "the command must have a name");
    }
    if (signal.durations.empty()) {
        return makeError(ErrorCode::Format, "the signal is empty — the remote received nothing");
    }
    if (impl_->model.findDevice(deviceId) == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    const formats::Bytes entry = ir::encodeRawEntry(signal);
    return detail::mutate(*impl_, [&](pugi::xml_node root, detail::IrData& ir) -> Result<void> {
        auto table = detail::writableTable(ir.ssIr, formats::kSsIrFileName);
        if (!table) {
            return std::unexpected(std::move(table.error()));
        }
        const std::size_t index = (*table)->intern(entry);
        if (index > detail::kMaxRawIndex || !formats::fitsOffsets(**table)) {
            return makeError(ErrorCode::Format, "no room for a new code in SsIr.bin");
        }

        pugi::xml_node device = detail::findById(root, "Device", deviceId);
        // Заново выученная — прежний сигнал заменяется.
        pugi::xml_node data = detail::commandWithoutData(device, name).append_child("Data");
        appendText(data, "Protocol", "-1");
        appendText(data, "Code", detail::rawCode(index));

        if (name == "PowerOn" || name == "PowerOff" || name == "PowerToggle") {
            describePower(root, device, deviceId);
        }
        return {};
    });
}

Result<bool> ConfigDocument::setProtocolCommand(const std::string& deviceId, const std::string& name,
                                                const std::string& protocol, std::uint32_t address,
                                                std::uint32_t command) {
    if (name.empty()) {
        return makeError(ErrorCode::Format, "the command must have a name");
    }
    if (impl_->model.findDevice(deviceId) == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    // Сигнал нужен в любом случае: он проверяет диапазоны и пригодится, если протоколом не выйдет.
    auto signal = ir::encode(protocol, address, command);
    if (!signal) {
        return std::unexpected(std::move(signal.error()));
    }
    // Таблица протоколов не читается или полна, пульт протокол так не хранит или проверка
    // исполнением не прошла — сигналом.
    if (auto native = ir::nativeCommand(protocol, address, command);
        native && storeNative(*impl_, deviceId, name, *native)) {
        return true;
    }
    if (auto learned = learnCommand(deviceId, name, *signal); !learned) {
        return std::unexpected(std::move(learned.error()));
    }
    return false;
}

Result<bool> ConfigDocument::storeLearned(const std::string& deviceId, const std::string& name,
                                          const ir::IrSignal& signal) {
    if (name.empty()) {
        return makeError(ErrorCode::Format, "the command must have a name");
    }
    if (impl_->model.findDevice(deviceId) == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    if (const auto decoded = ir::nativeProtocolOf(signal)) {
        if (auto native = ir::nativeCommand(decoded->protocol, decoded->address, decoded->command);
            native && storeNative(*impl_, deviceId, name, *native)) {
            return true;
        }
    }
    // Сохраняется сам выученный сигнал, а не собранный заново: у него родная несущая.
    if (auto learned = learnCommand(deviceId, name, signal); !learned) {
        return std::unexpected(std::move(learned.error()));
    }
    return false;
}

std::string ConfigDocument::CommandUse::text() const {
    switch (place) {
    case Place::DeviceButtons: return "device buttons";
    case Place::ActivityButtons: return "buttons of activity “" + name + "”";
    case Place::DeviceStates: return "power or inputs of the device";
    case Place::ActivityStart: return "start of activity “" + name + "”";
    case Place::MacroButton: return "macro button " + name;
    }
    return {};
}

std::vector<ConfigDocument::CommandUse> ConfigDocument::commandUsage(const std::string& deviceId,
                                                                     const std::string& name) const {
    std::vector<CommandUse> places;
    const std::string action = deviceId + "_" + name + "_";
    auto usesIn = [&](const std::vector<model::ControlGroup>& groups) {
        for (const auto& group : groups) {
            for (const auto& button : group.buttons) {
                // После модификатора «_» нет: «Input» не должна находить кнопки «Input_HDMI1».
                if (button.actionId.starts_with(action) && button.actionId.find('_', action.size()) == std::string::npos) {
                    return true;
                }
            }
        }
        return false;
    };
    const model::Device* device = impl_->model.findDevice(deviceId);
    if (device != nullptr && usesIn(device->controlGroups)) {
        places.push_back({CommandUse::Place::DeviceButtons, {}});
    }
    for (const auto& activity : impl_->model.activities) {
        if (usesIn(activity.controlGroups)) {
            places.push_back({CommandUse::Place::ActivityButtons, activity.label});
        }
    }

    // Действия «отправить команду» — в описании питания и входов и при запуске активностей.
    pugi::xml_document dom;
    if (!detail::loadDom(dom, impl_->xml)) {
        return places;
    }
    auto sends = [&](pugi::xml_node scope) {
        bool found = false;
        struct Walker final : pugi::xml_tree_walker {
            const std::string* deviceId = nullptr;
            const std::string* name = nullptr;
            bool* found = nullptr;
            bool for_each(pugi::xml_node& node) override {
                if (std::string_view(node.name()) == "Operation") {
                    const auto device = detail::textOf(node.find_child_by_attribute("Parameter", "name", "DeviceId"));
                    const auto command = detail::textOf(node.find_child_by_attribute("Parameter", "name", "Command"));
                    if (device == *deviceId && command == *name) {
                        *found = true;
                    }
                }
                return !*found;
            }
        } walker;
        walker.deviceId = &deviceId;
        walker.name = &name;
        walker.found = &found;
        scope.traverse(walker);
        return found;
    };
    const pugi::xml_node root = dom.child("Root");
    if (pugi::xml_node node = detail::findById(root, "Device", deviceId); node && sends(node.child("States"))) {
        places.push_back({CommandUse::Place::DeviceStates, {}});
    }
    for (pugi::xml_node activity : root.children("Activity")) {
        if (sends(activity.child("EnterActions"))) {
            places.push_back({CommandUse::Place::ActivityStart,
                              std::string(detail::textOf(activity.child("Presentation").child("Label")))});
        }
    }
    // Макросы — шаги в ActionLists.xml.
    pugi::xml_document lists;
    if (!impl_->actionLists.empty() && detail::loadDom(lists, impl_->actionLists)) {
        for (pugi::xml_node list : lists.child("Root").children("ActionList")) {
            const std::string listName = list.attribute("name").value();
            if (isMacroAction(listName) && sends(list)) {
                places.push_back({CommandUse::Place::MacroButton, listName});
            }
        }
    }
    return places;
}

Result<void> ConfigDocument::removeCommand(const std::string& deviceId, const std::string& name) {
    if (const auto usage = commandUsage(deviceId, name); !usage.empty()) {
        std::string list;
        for (const auto& place : usage) {
            list += (list.empty() ? "" : ", ") + place.text();
        }
        return makeError(ErrorCode::Unsupported, "the command is used: %1", {list});
    }
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node device = detail::findById(root, "Device", deviceId);
        if (!device) {
            return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
        }
        pugi::xml_node commands = device.child("Commands");
        for (pugi::xml_node command : commands.children("Command")) {
            if (detail::textOf(command.child("Name")) == name) {
                commands.remove_child(command);
                return {};
            }
        }
        return makeError(ErrorCode::NotFound, "no command “%1”", {name});
    });
}

Result<void> ConfigDocument::clearCommandUses(const std::string& deviceId, const std::string& name) {
    const model::Device* device = impl_->model.findDevice(deviceId);
    if (device == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    if (device->findCommand(name) == nullptr) {
        return makeError(ErrorCode::NotFound, "no command “%1”", {name});
    }
    // ActionId: <устройство>_<команда>_<модификатор>; после модификатора «_» нет — так команда
    // «Input» не захватит кнопки команды «Input_HDMI1».
    const std::string prefix = deviceId + "_" + name + "_";
    auto sendsCommand = [&prefix](pugi::xml_node button) {
        const std::string_view action = detail::textOf(button.child("ActionId"));
        return action.starts_with(prefix) && action.find('_', prefix.size()) == std::string_view::npos;
    };
    auto clearButtons = [&](pugi::xml_node owner) {
        pugi::xml_node presentation = owner.child("Presentation");
        for (pugi::xml_node group = presentation.child("ControlGroup"); group;) {
            pugi::xml_node nextGroup = group.next_sibling("ControlGroup");
            for (pugi::xml_node button = group.child("Button"); button;) {
                pugi::xml_node next = button.next_sibling("Button");
                if (sendsCommand(button)) {
                    group.remove_child(button);
                }
                button = next;
            }
            if (!group.child("Button")) {
                presentation.remove_child(group);
            }
            group = nextGroup;
        }
    };
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        clearButtons(detail::findById(root, "Device", deviceId));
        for (pugi::xml_node activity : root.children("Activity")) {
            clearButtons(activity);
            pugi::xml_node enter = activity.child("EnterActions");
            for (pugi::xml_node action = enter.child("Action"); action;) {
                pugi::xml_node next = action.next_sibling("Action");
                const pugi::xml_node operation = action.child("Operation");
                if (detail::textOf(operation.find_child_by_attribute("Parameter", "name", "DeviceId")) == deviceId &&
                    detail::textOf(operation.find_child_by_attribute("Parameter", "name", "Command")) == name) {
                    enter.remove_child(action);
                }
                action = next;
            }
            if (enter && !enter.first_child()) {
                activity.remove_child(enter);
            }
        }
        return {};
    });
}

std::optional<ir::IrSignal> ConfigDocument::commandSignal(const std::string& deviceId, const std::string& name) const {
    auto details = commandDetails(deviceId, name);
    return details ? std::optional(std::move(details->signal)) : std::nullopt;
}

std::string ConfigDocument::CommandDetails::rawData() const {
    if (decoded) {
        return decoded->rawData();
    }
    return ir::bitsToHex(codeBits);
}

Result<std::size_t> ConfigDocument::recoverIrTables(const ConfigDocument& source) {
    detail::IrData candidate = *impl_->ir;
    std::size_t recovered = 0;
    for (const bool raw : {false, true}) {
        auto& target = raw ? candidate.ssIr : candidate.irProto;
        if (target.table) continue;
        const auto& donor = raw ? source.impl_->ir->ssIr : source.impl_->ir->irProto;
        const bool needed = std::ranges::any_of(impl_->model.devices, [raw](const auto& device) {
            return std::ranges::any_of(device.commands, [raw](const auto& command) {
                return (command.protocol == -1) == raw;
            });
        });
        if (!needed) continue;
        if (!donor.table) {
            return makeError(ErrorCode::Format, "the backup has no readable %1", {raw ? "SsIr.bin" : "IrProto.bin"});
        }
        if (!raw) {
            const auto hash = impl_->model.property("ProtocolCacheHash");
            auto donorFile = formats::buildIrProto(*donor.table);
            if (!donorFile) {
                return wrapError(donorFile.error(), "the backup protocol table cannot be used");
            }
            if (hash.empty() || hash != formats::protocolHash(*donorFile)) {
                return makeError(ErrorCode::Format, "the backup protocol table has a different hash");
            }
        }
        for (const auto& device : impl_->model.devices) {
            const auto* otherDevice = source.impl_->model.findDevice(device.id);
            for (const auto& command : device.commands) {
                if ((command.protocol == -1) != raw) continue;
                const auto* other = otherDevice ? otherDevice->findCommand(command.name) : nullptr;
                if (!other || other->protocol != command.protocol || other->code != command.code) {
                    return makeError(ErrorCode::Format, "the backup has a different reference for command %1", {command.name});
                }
                const auto details = source.commandDetails(device.id, command.name);
                if (!details || details->signal.durations.empty()) {
                    return makeError(ErrorCode::Format, "the backup cannot render command %1", {command.name});
                }
            }
        }
        target = donor;
        ++recovered;
    }
    impl_->ir = std::make_shared<const detail::IrData>(std::move(candidate));
    return recovered;
}

Result<ConfigDocument::CommandDetails> ConfigDocument::commandDetails(const std::string& deviceId,
                                                                      const std::string& name) const {
    const model::Device* device = impl_->model.findDevice(deviceId);
    const model::Command* command = device != nullptr ? device->findCommand(name) : nullptr;
    if (command == nullptr) {
        return makeError(ErrorCode::NotFound, "no command “%1”", {name});
    }
    CommandDetails details;
    if (command->protocol == -1) {
        // Выученная: <Code>0xFFFF{номер}00</Code>, сигнал — в SsIr.bin.
        const auto& rawFile = impl_->ir->ssIr;
        if (!rawFile.present) {
            return makeError(ErrorCode::Format, "SsIr.bin is missing: restore the signal table from the original remote backup");
        }
        if (rawFile.error) return std::unexpected(*rawFile.error);
        const auto& table = rawFile.table;
        const auto index = detail::rawIndex(command->code);
        if (!table || !index || *index >= table->entries.size()) {
            return makeError(ErrorCode::Format, "the learned command's signal is not in SsIr.bin");
        }
        auto signal = ir::decodeRawEntry(table->entries[*index]);
        if (!signal) {
            return std::unexpected(std::move(signal.error()));
        }
        details.learned = true;
        details.signal = std::move(*signal);
    } else {
        // По протоколу: биты из <Code>, тайминги — из записи IrProto.bin.
        const auto& protocolFile = impl_->ir->irProto;
        if (!protocolFile.present) {
            return makeError(ErrorCode::Format, "IrProto.bin is missing: this command requires the protocol table from the original remote backup");
        }
        if (protocolFile.error) {
            return std::unexpected(*protocolFile.error);
        }
        const auto& table = protocolFile.table;
        const auto index = static_cast<std::size_t>(command->protocol);
        if (!table || index >= table->entries.size()) {
            return makeError(ErrorCode::Format, "protocol %1 is not in IrProto.bin", {std::to_string(command->protocol)});
        }
        const formats::Bytes& entry = table->entries[index];
        auto code = ir::parseProtocolCode(command->code);
        Result<ir::IrSignal> signal = !code ? Result<ir::IrSignal>(std::unexpected(code.error()))
                                      : simpleLayout(entry) ? ir::renderProtocol(entry, *code)
                                                            : Result<ir::IrSignal>(ir::IrSignal{});
        if (code) {
            details.codeBits = code->bits();
        }
        // Простой разбор понимает не все коды (этап нажатия пуст, как у RC5 и RC6; заголовок
        // отдельным элементом, как у JVC). Тогда — исполнение, как на пульте.
        if (!signal || signal->durations.empty()) {
            auto bytes = ir::codeBytes(command->code);
            auto run = bytes ? ir::runCarrier(entry, *bytes) : std::unexpected(bytes.error());
            if (run && !run->signal.durations.empty()) {
                signal = std::move(run->signal);
            } else if (!signal) {
                return std::unexpected(std::move(run ? signal.error() : run.error()));
            }
        }
        details.signal = std::move(*signal);
    }
    details.decoded = ir::decode(details.signal);
    return details;
}

namespace {

/// Короткое описание сигнала для таблицы сравнения.
std::string describeSignal(const Result<ConfigDocument::CommandDetails>& details) {
    if (!details) {
        return "?";
    }
    if (details->decoded) {
        return details->decoded->text();
    }
    return details->learned ? "learned" : "?";
}

/// Сравнение строк без учёта регистра (для порядка в таблице).
bool lessNoCase(const std::string& a, const std::string& b) {
    return std::ranges::lexicographical_compare(a, b, [](unsigned char x, unsigned char y) {
        return std::tolower(x) < std::tolower(y);
    });
}

} // namespace

std::vector<CommandComparison> ConfigDocument::compareDeviceCommands(const std::string& firstId,
                                                                     const std::string& secondId) const {
    const model::Device* first = impl_->model.findDevice(firstId);
    const model::Device* second = impl_->model.findDevice(secondId);
    if (first == nullptr || second == nullptr) {
        return {};
    }
    std::vector<CommandComparison> result;
    for (const auto& command : first->commands) {
        CommandComparison item;
        item.name = command.name;
        const auto mine = commandDetails(firstId, command.name);
        item.first = describeSignal(mine);
        const model::Command* other = second->findCommand(command.name);
        if (other == nullptr) {
            item.status = CommandComparison::Status::OnlyFirst;
        } else {
            const auto theirs = commandDetails(secondId, command.name);
            item.second = describeSignal(theirs);
            bool same = other->protocol == command.protocol && other->code == command.code;
            if (!same && mine && theirs) {
                same = ir::sameSignal(mine->signal, theirs->signal);
            }
            item.status = same ? CommandComparison::Status::Same : CommandComparison::Status::Different;
        }
        result.push_back(std::move(item));
    }
    for (const auto& command : second->commands) {
        if (first->findCommand(command.name) == nullptr) {
            result.push_back({command.name, CommandComparison::Status::OnlySecond, {},
                              describeSignal(commandDetails(secondId, command.name))});
        }
    }
    std::ranges::stable_sort(result, lessNoCase, &CommandComparison::name);
    return result;
}

Result<int> ConfigDocument::copyCommands(const std::string& fromId, const std::string& toId,
                                         const std::vector<std::string>& names) {
    const model::Device* from = impl_->model.findDevice(fromId);
    if (from == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {fromId});
    }
    if (impl_->model.findDevice(toId) == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {toId});
    }
    if (fromId == toId) {
        return makeError(ErrorCode::Format, "commands cannot be copied to the same device");
    }
    for (const auto& name : names) {
        if (from->findCommand(name) == nullptr) {
            return makeError(ErrorCode::NotFound, "no command “%1”", {name});
        }
    }
    int copied = 0;
    auto changed = detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        const pugi::xml_node source = detail::findById(root, "Device", fromId);
        pugi::xml_node target = detail::findById(root, "Device", toId);
        bool power = false;
        for (const auto& name : names) {
            pugi::xml_node data;
            for (pugi::xml_node node : source.child("Commands").children("Command")) {
                if (detail::textOf(node.child("Name")) == name) {
                    data = node.child("Data");
                }
            }
            detail::commandWithoutData(target, name).append_copy(data);
            ++copied;
            power = power || name == "PowerOn" || name == "PowerOff" || name == "PowerToggle";
        }
        if (power) {
            describePower(root, target, toId);
        }
        return {};
    });
    if (!changed) {
        return std::unexpected(std::move(changed.error()));
    }
    return copied;
}

// --- Общие помощники устройств (объявлены в configDocumentImpl.h) ---------------------

namespace detail {

std::string uniqueId(const model::RemoteConfig& config) {
    static std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> digits(10'000'000, 99'999'999);
    for (;;) {
        std::string id = std::to_string(digits(generator));
        if (config.findDevice(id) == nullptr && config.findActivity(id) == nullptr) {
            return id;
        }
    }
}

std::string uniqueLabel(const model::RemoteConfig& config, const std::string& label) {
    auto taken = [&](const std::string& candidate) {
        return std::ranges::any_of(config.devices,
                                   [&](const model::Device& device) { return device.displayName() == candidate; });
    };
    if (!taken(label)) {
        return label;
    }
    for (int n = 2;; ++n) {
        std::string candidate = label + " (" + std::to_string(n) + ")";
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

std::string rawCode(std::size_t index) {
    char text[16];
    std::snprintf(text, sizeof text, "0xFFFF%02X%02X", static_cast<unsigned>(index & 0xFF),
                  static_cast<unsigned>((index >> 8) & 0xFF));
    return text;
}

std::optional<std::size_t> rawIndex(std::string_view code) {
    if (code.size() < 10 || !(code.starts_with("0xFFFF") || code.starts_with("0xffff"))) {
        return std::nullopt;
    }
    unsigned low = 0;
    unsigned high = 0;
    if (std::from_chars(code.data() + 6, code.data() + 8, low, 16).ec != std::errc{} ||
        std::from_chars(code.data() + 8, code.data() + 10, high, 16).ec != std::errc{}) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(low | (high << 8));
}

pugi::xml_node appendDevice(pugi::xml_node root, const std::string& id, const DeviceDraft& draft) {
    pugi::xml_node last;
    for (pugi::xml_node node : root.children("Device")) {
        last = node;
    }
    pugi::xml_node anchor = last ? last : root.child("Controller");
    pugi::xml_node device = anchor ? root.insert_child_after("Device", anchor) : root.prepend_child("Device");
    appendText(device, "Id", id);
    appendText(device, "Type", draft.type);
    appendText(device, "Manufacturer", draft.manufacturer);
    appendText(device, "Model", draft.model);
    appendText(device.append_child("Presentation"), "Label", draft.label);
    pugi::xml_node properties = device.append_child("Properties");
    appendProperty(properties, "AlwaysOn", "false");
    appendProperty(properties, "ManualPower", "true"); // пока нет команд питания
    // Паузы между нажатиями — как у устройств из базы Logitech.
    pugi::xml_node timing = device.append_child("Commands").append_child("Properties");
    appendProperty(timing, "PressPreSilence", "500");
    appendProperty(timing, "PressInterKey", "100");
    appendProperty(timing, "HoldPreSilence", "50");
    appendProperty(timing, "HoldInterKey", "100");

    // В существующих активностях новое устройство не участвует.
    for (pugi::xml_node activity : root.children("Activity")) {
        pugi::xml_node power = activity.child("Power");
        if (!power) {
            power = activity.append_child("Power");
        }
        appendText(power, "Off", id);
    }
    return device;
}

void appendSendCommand(pugi::xml_node parent, const std::string& deviceId, const std::string& command) {
    pugi::xml_node action = parent.append_child("Action");
    appendText(action, "Target", "Device");
    pugi::xml_node operation = action.append_child("Operation");
    appendText(operation, "Name", "SendCommand");
    appendText(operation, "Parameter", deviceId).append_attribute("name") = "DeviceId";
    appendText(operation, "Parameter", command).append_attribute("name") = "Command";
    appendText(operation, "Parameter", "Press").append_attribute("name") = "Modifier";
}

void enablePowerOff(pugi::xml_node root, pugi::xml_node device, const std::string& deviceId) {
    if (pugi::xml_node properties = device.child("Properties")) {
        setProperty(properties, "ManualPower", "false");
    }
    for (pugi::xml_node activity : root.children("Activity")) {
        if (textOf(activity.child("Type")) == "PowerOff") {
            pugi::xml_node enter = activity.child("EnterActions");
            if (!enter) {
                enter = activity.child("Power") ? activity.insert_child_before("EnterActions", activity.child("Power"))
                                                : activity.append_child("EnterActions");
            }
            appendSetValue(enter, deviceId, "Power", "Off");
        }
    }
}

pugi::xml_node commandWithoutData(pugi::xml_node device, const std::string& name) {
    pugi::xml_node commands = device.child("Commands");
    if (!commands) {
        commands = device.append_child("Commands");
    }
    pugi::xml_node command;
    for (pugi::xml_node node : commands.children("Command")) {
        if (textOf(node.child("Name")) == name) {
            command = node;
        }
    }
    if (command) {
        command.remove_child("Data");
    } else {
        command = commands.append_child("Command");
        appendText(command, "Name", name);
    }
    return command;
}

std::string delayText(pugi::xml_node operation) {
    // Пульт читает «Delay» (так пишут конфигурации Logitech, по данным afterglow).
    return std::string(textOf(operation.find_child_by_attribute("Parameter", "name", "Delay")));
}

} // namespace detail

} // namespace harmony
