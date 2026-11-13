/**
 * @file configDocumentAdd.cpp
 * @brief Добавление в конфигурацию: перенос устройства из другой конфигурации и новая активность.
 */

#include "configDocumentImpl.h"

#include "activityTemplate.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <random>
#include <set>

namespace harmony {

Result<void> detail::updateProtocolHash(pugi::xml_node root, const TableFile& irProto) {
    if (!irProto.present || !irProto.table) {
        return {};
    }
    auto file = formats::buildIrProto(*irProto.table);
    if (!file) {
        return std::unexpected(std::move(file.error()));
    }
    const std::string hash = formats::protocolHash(*file);
    if (pugi::xml_node properties = root.child("Properties")) {
        setProperty(properties, "ProtocolCacheHash", hash);
    }
    pugi::xml_node protocols = root.child("Protocols");
    if (!protocols) {
        protocols = root.append_child("Protocols");
    }
    pugi::xml_node node = protocols.child("Hash");
    if (!node) {
        node = protocols.append_child("Hash");
    }
    node.text() = hash.c_str();
    return {};
}

Result<formats::IrTable*> detail::writableTable(TableFile& file, std::string_view fileName) {
    if (!file.present) {
        file.present = true;
        file.table = formats::IrTable{};
    }
    if (!file.table) {
        return wrapError(ErrorCode::Format, *file.error, "%1 of this configuration is not readable",
                         {std::string(fileName)});
    }
    return &*file.table;
}

namespace {

using detail::appendText;


/// Заменяет id устройства во всём его описании: <Id>, DeviceId действий, префиксы ActionId.
void renameDeviceId(pugi::xml_node device, const std::string& from, const std::string& to) {
    struct Walker final : pugi::xml_tree_walker {
        const std::string* from = nullptr;
        const std::string* to = nullptr;
        bool for_each(pugi::xml_node& node) override {
            if (node.type() != pugi::node_pcdata) {
                return true;
            }
            const std::string text = node.value();
            if (text == *from) {
                node.set_value(to->c_str());
            } else if (text.starts_with(*from + "_")) {
                node.set_value((*to + text.substr(from->size())).c_str());
            }
            return true;
        }
    } walker;
    walker.from = &from;
    walker.to = &to;
    device.traverse(walker);
}

/// Число из 16 бит little endian, записанное в коде команды с позиции @p byte («0x» не считая).
std::optional<unsigned> codeWord(const std::string& code, std::size_t byte) {
    const std::size_t at = 2 + byte * 2;
    if (!code.starts_with("0x") || code.size() < at + 4) {
        return std::nullopt;
    }
    unsigned low = 0;
    unsigned high = 0;
    const char* text = code.data() + at;
    if (std::from_chars(text, text + 2, low, 16).ec != std::errc{} ||
        std::from_chars(text + 2, text + 4, high, 16).ec != std::errc{}) {
        return std::nullopt;
    }
    return low | (high << 8);
}

/// Записывает 16-битное число в код команды, сохраняя регистр шестнадцатеричных цифр.
void setCodeWord(std::string& code, std::size_t byte, unsigned value) {
    const bool lower = std::ranges::any_of(code.substr(2), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) != 0;
    });
    char text[5];
    std::snprintf(text, sizeof text, lower ? "%02x%02x" : "%02X%02X", value & 0xFFU, (value >> 8) & 0xFFU);
    code.replace(2 + byte * 2, 4, text);
}

/// Запись таблицы источника по номеру.
Result<const formats::Bytes*> sourceEntry(const detail::TableFile& file, std::string_view fileName,
                                          std::size_t index) {
    const std::string name(fileName);
    if (!file.present) {
        return makeError(ErrorCode::Format, "the source configuration has no file %1", {name});
    }
    if (!file.table) {
        return wrapError(ErrorCode::Format, *file.error, "%1 of the source is not readable", {name});
    }
    if (index >= file.table->entries.size()) {
        return makeError(ErrorCode::Format, "%1 of the source: no entry %2", {name, std::to_string(index)});
    }
    return &file.table->entries[index];
}

/**
 * @brief Переносит ИК-данные команд устройства в таблицы документа и перенумеровывает ссылки.
 *
 * Номер протокола стоит и в <Protocol>, и в первых двух байтах <Code>;
 * у сырого кода (<Protocol>-1) номер записи SsIr.bin — во вторых двух байтах.
 */
Result<void> remapCommands(pugi::xml_node device, const detail::IrData& source, detail::IrData& target) {
    for (pugi::xml_node command : device.child("Commands").children("Command")) {
        pugi::xml_node data = command.child("Data");
        pugi::xml_node protocolNode = data.child("Protocol");
        pugi::xml_node codeNode = data.child("Code");
        const std::string name = command.child("Name").text().get();
        const int protocol = protocolNode.text().as_int(-2);
        std::string code = codeNode.text().get();
        // Причина — отдельный текст (переводится в интерфейсе), уточнение — имя команды.
        auto fail = [&](const char* why) {
            return wrapError(ErrorCode::Format, Error(ErrorCode::Format, why), "command “%1”", {name});
        };

        if (protocol >= 0) {
            if (codeWord(code, 0) != static_cast<unsigned>(protocol)) {
                return fail("the code does not start with the protocol number");
            }
            auto entry = sourceEntry(source.irProto, formats::kIrProtoFileName,
                                     static_cast<std::size_t>(protocol));
            if (!entry) {
                return std::unexpected(std::move(entry.error()));
            }
            auto table = detail::writableTable(target.irProto, formats::kIrProtoFileName);
            if (!table) {
                return std::unexpected(std::move(table.error()));
            }
            const std::size_t index = (*table)->intern(**entry);
            if (index > detail::kMaxProtocolIndex || !formats::fitsOffsets(**table)) {
                return makeError(ErrorCode::Format, "no room for a new protocol in IrProto.bin");
            }
            protocolNode.text() = static_cast<int>(index);
            setCodeWord(code, 0, static_cast<unsigned>(index));
        } else if (protocol == -1) {
            const auto rawIndex = codeWord(code, 2);
            if (codeWord(code, 0) != 0xFFFFU || !rawIndex) {
                return fail("unknown raw code reference format");
            }
            auto entry = sourceEntry(source.ssIr, formats::kSsIrFileName, *rawIndex);
            if (!entry) {
                return std::unexpected(std::move(entry.error()));
            }
            auto table = detail::writableTable(target.ssIr, formats::kSsIrFileName);
            if (!table) {
                return std::unexpected(std::move(table.error()));
            }
            const std::size_t index = (*table)->intern(**entry);
            if (index > detail::kMaxRawIndex || !formats::fitsOffsets(**table)) {
                return makeError(ErrorCode::Format, "no room for a new code in SsIr.bin");
            }
            setCodeWord(code, 2, static_cast<unsigned>(index));
        } else {
            return fail("invalid protocol number");
        }
        codeNode.text() = code.c_str();
    }
    return {};
}

using detail::updateProtocolHash;

/// Дочерний элемент; если его нет — создаётся перед @p before (или в конце).
pugi::xml_node ensureChild(pugi::xml_node parent, const char* name, const char* before = nullptr) {
    if (pugi::xml_node node = parent.child(name)) {
        return node;
    }
    pugi::xml_node next = before != nullptr ? parent.child(before) : pugi::xml_node();
    return next ? parent.insert_child_before(name, next) : parent.append_child(name);
}

/// Место для нового <Device>: после последнего устройства, иначе перед активностями.
pugi::xml_node insertDevice(pugi::xml_node root, pugi::xml_node device) {
    pugi::xml_node lastDevice;
    for (pugi::xml_node node : root.children("Device")) {
        lastDevice = node;
    }
    if (lastDevice) {
        return root.insert_copy_after(device, lastDevice);
    }
    if (pugi::xml_node controller = root.child("Controller")) {
        return root.insert_copy_after(device, controller);
    }
    if (pugi::xml_node activity = root.child("Activity")) {
        return root.insert_copy_before(device, activity);
    }
    return root.append_copy(device);
}

} // namespace

Result<void> ConfigDocument::checkIrData() const {
    bool usesProtocols = false;
    bool usesRawCodes = false;
    for (const auto& device : impl_->model.devices) {
        for (const auto& command : device.commands) {
            usesProtocols = usesProtocols || command.protocol >= 0;
            usesRawCodes = usesRawCodes || command.protocol == -1;
        }
    }
    auto check = [](bool used, const detail::TableFile& file, std::string_view fileName) -> Result<void> {
        const std::string name(fileName);
        if (!used) {
            return {};
        }
        if (!file.present) {
            return makeError(ErrorCode::Format, "file %1 is missing — IR commands cannot be copied without it", {name});
        }
        if (!file.table) {
            return wrapError(ErrorCode::Format, *file.error, "file %1 could not be read", {name});
        }
        return {};
    };
    if (auto protocols = check(usesProtocols, impl_->ir->irProto, formats::kIrProtoFileName); !protocols) {
        return protocols;
    }
    return check(usesRawCodes, impl_->ir->ssIr, formats::kSsIrFileName);
}

Result<std::string> ConfigDocument::importDevice(const ConfigDocument& source, const std::string& deviceId) {
    const Impl& from = *source.impl_;
    const model::Device* sourceDevice = from.model.findDevice(deviceId);
    if (sourceDevice == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1 in the source", {deviceId});
    }
    pugi::xml_document sourceDom;
    if (auto loaded = detail::loadDom(sourceDom, from.xml); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    const pugi::xml_node sourceNode = detail::findById(sourceDom.child("Root"), "Device", deviceId);

    // Id и имя — до правки: они зависят от текущего состава конфигурации.
    const model::RemoteConfig& config = impl_->model;
    const bool idTaken = config.findDevice(deviceId) != nullptr || config.findActivity(deviceId) != nullptr;
    const std::string newId = idTaken ? detail::uniqueId(config) : deviceId;
    const std::string label = detail::uniqueLabel(config, sourceDevice->displayName());
    const bool needsPowerOff = sourceDevice->powerControlled;

    auto added = detail::mutate(*impl_, [&](pugi::xml_node root, detail::IrData& ir) -> Result<void> {
        pugi::xml_node device = insertDevice(root, sourceNode);
        if (newId != deviceId) {
            renameDeviceId(device, deviceId, newId);
        }
        if (label != sourceDevice->displayName() || !sourceDevice->label.empty()) {
            detail::setLabel(device, label);
        }
        if (auto remapped = remapCommands(device, *from.ir, ir); !remapped) {
            return remapped;
        }

        // Новое устройство не участвует в активностях: каждая его выключает.
        for (pugi::xml_node activity : root.children("Activity")) {
            const bool powerOff = std::string_view(activity.child("Type").text().get()) == "PowerOff";
            if (powerOff && needsPowerOff) {
                detail::appendSetValue(ensureChild(activity, "EnterActions", "Power"), newId, "Power", "Off");
            }
            appendText(ensureChild(activity, "Power"), "Off", newId);
        }
        return updateProtocolHash(root, ir.irProto);
    });
    if (!added) {
        return std::unexpected(std::move(added.error()));
    }
    return newId;
}

namespace {

/**
 * @brief Проверяет черновик активности и дополняет его: управление по умолчанию — первое устройство.
 */
Result<ActivityDraft> checkDraft(const model::RemoteConfig& config, ActivityDraft draft) {
    if (draft.label.empty()) {
        return makeError(ErrorCode::Format, "the activity must have a name");
    }
    if (draft.devices.empty()) {
        return makeError(ErrorCode::Format, "an activity must include at least one device");
    }
    if (std::ranges::find(activityTypes(), draft.type) == activityTypes().end()) {
        return makeError(ErrorCode::Format, "unknown activity type “%1”", {draft.type});
    }
    for (const auto& id : draft.devices) {
        if (config.findDevice(id) == nullptr) {
            return makeError(ErrorCode::NotFound, "no device %1", {id});
        }
    }
    auto inDraft = [&](const std::string& id) { return std::ranges::find(draft.devices, id) != draft.devices.end(); };
    if (draft.controlDevice.empty()) {
        draft.controlDevice = draft.devices.front();
    }
    for (const std::string* role : {&draft.controlDevice, &draft.volumeDevice, &draft.displayDevice}) {
        if (!role->empty() && !inDraft(*role)) {
            return makeError(ErrorCode::Format, "the role's device is not turned on in the activity: %1", {*role});
        }
    }
    for (const auto& [deviceId, input] : draft.inputs) {
        const model::Device* device = config.findDevice(deviceId);
        if (device == nullptr || !inDraft(deviceId) ||
            std::ranges::find(device->inputs, input) == device->inputs.end()) {
            return makeError(ErrorCode::Format, "device %1 has no input “%2”", {deviceId, input});
        }
    }
    return draft;
}

template <typename T>
bool contains(const std::vector<T>& list, const T& value) {
    return std::ranges::find(list, value) != list.end();
}

/// Свойства активности по её типу (заменяют прежние).
void fillProperties(pugi::xml_node properties, const std::string& type) {
    while (properties.first_child()) {
        properties.remove_child(properties.first_child());
    }
    for (const auto& [name, value] : detail::activityProperties(type)) {
        detail::appendProperty(properties, name.c_str(), value);
    }
}

/// Группа аппаратных кнопок по плану назначения.
void appendHardButtons(pugi::xml_node presentation, const std::vector<HardButtonAssignment>& plan) {
    if (plan.empty()) {
        return;
    }
    pugi::xml_node group = presentation.append_child("ControlGroup");
    group.append_attribute("name") = "HardButtons";
    for (const auto& button : plan) {
        pugi::xml_node node = group.append_child("Button");
        node.append_attribute("name") = button.key.c_str();
        appendText(node, "Label", "");
        appendText(node, "ActionId", button.deviceId + "_" + button.command + "_Hold");
    }
}

/// Роли перед @p before (или в конце): управление, изображение, громкость — как у пульта.
void insertRoles(pugi::xml_node activity, pugi::xml_node before, const ActivityDraft& draft) {
    auto addRole = [&](const char* name, const std::string& deviceId) {
        if (deviceId.empty()) {
            return;
        }
        pugi::xml_node role = before ? activity.insert_child_before("Role", before) : activity.append_child("Role");
        appendText(role, "Name", name);
        appendText(role, "DeviceId", deviceId);
        role.append_child("Presentation");
    };
    addRole("DEFAULT", draft.controlDevice);
    addRole("DISPLAY", draft.displayDevice);
    addRole("VOLUME", draft.volumeDevice.empty() ? draft.controlDevice : draft.volumeDevice);
}

/// Питание: включаются устройства черновика, остальные выключаются.
void fillPower(pugi::xml_node power, const model::RemoteConfig& config, const ActivityDraft& draft) {
    while (power.first_child()) {
        power.remove_child(power.first_child());
    }
    for (const auto& deviceId : draft.devices) {
        appendText(power, "On", deviceId);
    }
    for (const auto& device : config.devices) {
        if (!contains(draft.devices, device.id)) {
            appendText(power, "Off", device.id);
        }
    }
}

/// Устройство, которому адресовано действие (<Parameter name="DeviceId">); пусто — нет.
std::string actionDevice(pugi::xml_node action) {
    return action.child("Operation").find_child_by_attribute("Parameter", "name", "DeviceId").text().get();
}

/// Действие — переключение входа (SetValue, State=Input).
bool isInputSwitch(pugi::xml_node action) {
    const pugi::xml_node operation = action.child("Operation");
    return std::string_view(operation.child("Name").text().get()) == "SetValue" &&
           std::string_view(operation.find_child_by_attribute("Parameter", "name", "State").text().get()) == "Input";
}

/// Устройство, которому шлёт команду кнопка (начало ActionId до «_»).
std::string buttonDevice(pugi::xml_node button) {
    const std::string action = button.child("ActionId").text().get();
    return action.substr(0, action.find('_'));
}

} // namespace

Result<std::string> ConfigDocument::addActivity(const ActivityDraft& request) {
    const model::RemoteConfig& config = impl_->model;
    auto checked = checkDraft(config, request);
    if (!checked) {
        return std::unexpected(std::move(checked.error()));
    }
    const ActivityDraft& draft = *checked;
    const std::string id = detail::uniqueId(config);
    const auto buttons = planHardButtons(config, draft);

    auto added = detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node lastActivity;
        for (pugi::xml_node node : root.children("Activity")) {
            lastActivity = node;
        }
        pugi::xml_node protocols = root.child("Protocols");
        pugi::xml_node activity = lastActivity ? root.insert_child_after("Activity", lastActivity)
                                  : protocols  ? root.insert_child_before("Activity", protocols)
                                               : root.append_child("Activity");
        appendText(activity, "Id", id);
        appendText(activity, "Type", draft.type);
        fillProperties(activity.append_child("Properties"), draft.type);

        pugi::xml_node presentation = activity.append_child("Presentation");
        appendText(presentation, "Label", draft.label);
        if (draft.type == "VirtualTelevisionN") {
            presentation.append_child("ChannelList"); // список каналов пульт ведёт сам
        }
        appendHardButtons(presentation, buttons);

        if (!draft.inputs.empty()) {
            pugi::xml_node enter = activity.append_child("EnterActions");
            for (const auto& [deviceId, input] : draft.inputs) {
                detail::appendSetValue(enter, deviceId, "Input", input);
            }
        }
        insertRoles(activity, {}, draft);
        fillPower(activity.append_child("Power"), config, draft);
        return {};
    });
    if (!added) {
        return std::unexpected(std::move(added.error()));
    }
    return id;
}

Result<void> ConfigDocument::updateActivity(const std::string& activityId, const ActivityDraft& request,
                                            bool reassignHardButtons) {
    const model::RemoteConfig& config = impl_->model;
    const model::Activity* current = config.findActivity(activityId);
    if (current == nullptr) {
        return makeError(ErrorCode::NotFound, "no activity %1", {activityId});
    }
    if (current->isPowerOff()) {
        return makeError(ErrorCode::Unsupported, "the remote itself defines the “turn everything off” setup");
    }
    auto checked = checkDraft(config, request);
    if (!checked) {
        return std::unexpected(std::move(checked.error()));
    }
    const ActivityDraft& draft = *checked;
    const auto plan = planHardButtons(config, draft);

    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        pugi::xml_node activity = detail::findById(root, "Activity", activityId);

        // Тип: свойства и список каналов — как у новой активности этого типа.
        pugi::xml_node presentation = activity.child("Presentation");
        if (draft.type != activity.child("Type").text().get()) {
            activity.child("Type").text() = draft.type.c_str();
            pugi::xml_node properties = activity.child("Properties");
            if (!properties) {
                properties = activity.insert_child_after("Properties", activity.child("Type"));
            }
            fillProperties(properties, draft.type);
            const bool television = draft.type == "VirtualTelevisionN";
            pugi::xml_node channels = presentation.child("ChannelList");
            if (television && !channels) {
                presentation.insert_child_after("ChannelList", presentation.child("Label"));
            } else if (!television && channels) {
                presentation.remove_child(channels);
            }
        }
        detail::setLabel(activity, draft.label);
        presentation = activity.child("Presentation");

        // Кнопки убранных устройств не сработают — удаляем; аппаратные при желании назначаем заново.
        for (pugi::xml_node group = presentation.child("ControlGroup"); group;) {
            pugi::xml_node nextGroup = group.next_sibling("ControlGroup");
            const bool hard = std::string_view(group.attribute("name").value()) == "HardButtons";
            if (hard && reassignHardButtons) {
                presentation.remove_child(group);
            } else {
                for (pugi::xml_node button = group.child("Button"); button;) {
                    pugi::xml_node next = button.next_sibling("Button");
                    if (!contains(draft.devices, buttonDevice(button))) {
                        group.remove_child(button);
                    }
                    button = next;
                }
                if (!group.child("Button")) {
                    presentation.remove_child(group);
                }
            }
            group = nextGroup;
        }
        if (reassignHardButtons) {
            appendHardButtons(presentation, plan);
        }

        // Действия при запуске: входы — по черновику и первыми; остальные сохраняются,
        // кроме адресованных убранным устройствам.
        pugi::xml_node enter = activity.child("EnterActions");
        std::vector<pugi::xml_node> kept;
        for (pugi::xml_node action : enter.children("Action")) {
            if (!isInputSwitch(action) && (actionDevice(action).empty() || contains(draft.devices, actionDevice(action)))) {
                kept.push_back(action);
            }
        }
        if (!enter && !draft.inputs.empty()) {
            pugi::xml_node before = activity.child("Role") ? activity.child("Role") : activity.child("Power");
            enter = before ? activity.insert_child_before("EnterActions", before) : activity.append_child("EnterActions");
        }
        if (enter) {
            for (pugi::xml_node action = enter.first_child(); action;) {
                pugi::xml_node next = action.next_sibling();
                if (!contains(kept, action)) {
                    enter.remove_child(action);
                }
                action = next;
            }
            // Входы — перед сохранёнными действиями, в порядке черновика.
            const pugi::xml_node first = enter.first_child();
            for (const auto& [deviceId, input] : draft.inputs) {
                detail::appendSetValue(enter, deviceId, "Input", input);
                if (first) {
                    enter.insert_move_before(enter.last_child(), first);
                }
            }
            if (!enter.first_child()) {
                activity.remove_child(enter);
            }
        }

        // Роли и питание — заново.
        while (pugi::xml_node role = activity.child("Role")) {
            activity.remove_child(role);
        }
        pugi::xml_node power = activity.child("Power");
        if (!power) {
            power = activity.append_child("Power");
        }
        insertRoles(activity, power, draft);
        fillPower(power, config, draft);
        return {};
    });
}

} // namespace harmony
