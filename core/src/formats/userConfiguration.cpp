/**
 * @file userConfiguration.cpp
 * @brief Разбор UserConfiguration.xml в модель конфигурации (pugixml).
 */

#include "harmony/core/formats/userConfiguration.h"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace harmony::formats {

namespace {

using model::Activity;
using model::ActivityRole;
using model::Button;
using model::Command;
using model::ControlGroup;
using model::Device;
using model::RemoteConfig;

/// Текст дочернего элемента @p name или пустая строка, если элемента нет.
std::string childText(const pugi::xml_node& node, const char* name) {
    return node.child(name).text().get();
}

/**
 * @brief Разбирает целое число из текста элемента.
 * @return Число; 0 для пустого текста; std::nullopt, если текст не число.
 */
std::optional<int> parseInt(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// Переводит смещение в тексте в номер строки (с 1) — для понятных сообщений об ошибках.
std::size_t lineAt(std::string_view text, std::ptrdiff_t offset) {
    const auto clamped = std::clamp<std::ptrdiff_t>(offset, 0, static_cast<std::ptrdiff_t>(text.size()));
    return 1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() + clamped, '\n'));
}

/// Путь в UTF-8 для сообщений об ошибках (на Windows path::string() портит кириллицу).
std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

/**
 * @brief Читает группы кнопок из элемента <Presentation>.
 * @param presentation Элемент <Presentation> устройства или активности.
 * @param isDevice Владелец — устройство (иначе активность); для сообщений об ошибках.
 * @param ownerId Идентификатор владельца.
 */
Result<std::vector<ControlGroup>> parseControlGroups(const pugi::xml_node& presentation,
                                                     bool isDevice, const std::string& ownerId) {
    std::vector<ControlGroup> groups;
    for (const pugi::xml_node groupNode : presentation.children("ControlGroup")) {
        ControlGroup group;
        group.name = groupNode.attribute("name").value();

        for (const pugi::xml_node buttonNode : groupNode.children("Button")) {
            Button button;
            button.name = buttonNode.attribute("name").value();
            button.label = childText(buttonNode, "Label");
            button.icon = childText(buttonNode, "Icon");
            button.actionId = childText(buttonNode, "ActionId");

            const std::string positionText = childText(buttonNode, "Position");
            const std::optional<int> position = parseInt(positionText);
            if (!position) {
                return makeError(ErrorCode::Format,
                                 isDevice ? "device %1: button “%2” has an invalid position “%3”"
                                          : "activity %1: button “%2” has an invalid position “%3”",
                                 {ownerId, button.label, positionText});
            }
            button.position = *position;
            group.buttons.push_back(std::move(button));
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

/// Есть ли у элемента пометка «не отправлять на пульт».
bool markedNotSent(const pugi::xml_node& node) {
    for (const pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_comment && std::string_view(child.value()) == model::kNotSentMarker) {
            return true;
        }
    }
    return false;
}

/// Читает один элемент <Device>.
Result<Device> parseDevice(const pugi::xml_node& node) {
    Device device;
    device.id = childText(node, "Id");
    if (device.id.empty()) {
        return makeError(ErrorCode::Format, "device without an <Id>");
    }
    device.type = childText(node, "Type");
    device.manufacturer = childText(node, "Manufacturer");
    device.model = childText(node, "Model");
    device.notSent = markedNotSent(node);

    const pugi::xml_node presentation = node.child("Presentation");
    device.label = childText(presentation, "Label");

    auto groups = parseControlGroups(presentation, true, device.id);
    if (!groups) {
        return std::unexpected(std::move(groups.error()));
    }
    device.controlGroups = std::move(*groups);

    // Состояния, которые пульт отслеживает: питание и текущий вход.
    for (const pugi::xml_node state : node.child("States").children("State")) {
        const std::string stateId = childText(state, "Id");
        if (stateId == "Power") {
            device.powerControlled = true;
        } else if (stateId == "Input") {
            for (const pugi::xml_node value : state.children("Value")) {
                device.inputs.emplace_back(value.text().get());
            }
        }
    }

    for (const pugi::xml_node commandNode : node.child("Commands").children("Command")) {
        Command command;
        command.name = childText(commandNode, "Name");
        const pugi::xml_node data = commandNode.child("Data");
        command.code = childText(data, "Code");

        const std::string protocolText = childText(data, "Protocol");
        const std::optional<int> protocol = parseInt(protocolText);
        if (!protocol) {
            return makeError(ErrorCode::Format, "device %1: command “%2” has an invalid protocol “%3”", {device.id, command.name, protocolText});
        }
        command.protocol = *protocol;
        // Необязательные поля конфигураций, собранных из других форматов (хаб); у 900 их нет.
        const std::string transport = childText(commandNode, "Transport");
        if (transport == "Bluetooth") command.transport = model::Transport::Bluetooth;
        else if (transport == "Network") command.transport = model::Transport::Network;
        else if (transport == "HomeAssistant") command.transport = model::Transport::HomeAssistant;
        else if (transport == "Unknown") command.transport = model::Transport::Unknown;
        command.protocolName = childText(commandNode, "ProtocolName");
        command.native = childText(commandNode, "Native");
        command.fromDatabase = childText(commandNode, "CodeSource") == "Database";
        device.commands.push_back(std::move(command));
    }
    return device;
}

/// Читает один элемент <Activity>.
Result<Activity> parseActivity(const pugi::xml_node& node) {
    Activity activity;
    activity.id = childText(node, "Id");
    if (activity.id.empty()) {
        return makeError(ErrorCode::Format, "activity without an <Id>");
    }
    activity.type = childText(node, "Type");
    activity.notSent = markedNotSent(node);

    const pugi::xml_node presentation = node.child("Presentation");
    activity.label = childText(presentation, "Label");

    auto groups = parseControlGroups(presentation, false, activity.id);
    if (!groups) {
        return std::unexpected(std::move(groups.error()));
    }
    activity.controlGroups = std::move(*groups);

    for (const pugi::xml_node roleNode : node.children("Role")) {
        activity.roles.push_back(
            ActivityRole{.name = childText(roleNode, "Name"), .deviceId = childText(roleNode, "DeviceId")});
    }

    // Переключение входов при запуске: SetValue с State=Input.
    for (const pugi::xml_node action : node.child("EnterActions").children("Action")) {
        const pugi::xml_node operation = action.child("Operation");
        if (childText(operation, "Name") != "SetValue") {
            continue;
        }
        std::string deviceId;
        std::string state;
        std::string value;
        for (const pugi::xml_node parameter : operation.children("Parameter")) {
            const std::string_view name = parameter.attribute("name").value();
            if (name == "DeviceId") {
                deviceId = parameter.text().get();
            } else if (name == "State") {
                state = parameter.text().get();
            } else if (name == "Value") {
                value = parameter.text().get();
            }
        }
        if (state == "Input" && !deviceId.empty()) {
            activity.inputs.emplace_back(deviceId, value);
        }
    }

    const pugi::xml_node power = node.child("Power");
    for (const pugi::xml_node on : power.children("On")) {
        activity.powerOn.emplace_back(on.text().get());
    }
    for (const pugi::xml_node off : power.children("Off")) {
        activity.powerOff.emplace_back(off.text().get());
    }
    return activity;
}

} // namespace

Result<model::RemoteConfig> parseUserConfiguration(std::string_view xml) {
    pugi::xml_document document;
    const pugi::xml_parse_result parsed =
        document.load_buffer(xml.data(), xml.size(), pugi::parse_default | pugi::parse_comments, pugi::encoding_utf8);
    if (!parsed) {
        return makeError(ErrorCode::Format, "XML error on line %1: %2", {std::to_string(lineAt(xml, parsed.offset)), parsed.description()});
    }

    const pugi::xml_node root = document.child("Root");
    if (!root) {
        return makeError(ErrorCode::Format, "this is not UserConfiguration.xml: no <Root> element");
    }

    RemoteConfig config;

    for (const pugi::xml_node property : root.child("Properties").children("Property")) {
        config.properties.emplace_back(property.attribute("name").value(), property.text().get());
    }

    for (const pugi::xml_node node : root.children("Device")) {
        auto device = parseDevice(node);
        if (!device) {
            return std::unexpected(std::move(device.error()));
        }
        config.devices.push_back(std::move(*device));
    }

    for (const pugi::xml_node node : root.children("Activity")) {
        auto activity = parseActivity(node);
        if (!activity) {
            return std::unexpected(std::move(activity.error()));
        }
        config.activities.push_back(std::move(*activity));
    }

    return config;
}

Result<model::RemoteConfig> readUserConfiguration(const std::filesystem::path& path) {
    // Разрешаем указывать как сам файл, так и папку резервной копии.
    std::error_code fsError;
    std::filesystem::path file = path;
    if (std::filesystem::is_directory(path, fsError)) {
        file = path / kUserConfigurationFileName;
    }

    if (!std::filesystem::exists(file, fsError)) {
        return makeError(ErrorCode::NotFound, "file not found: %1", {pathToUtf8(file)});
    }

    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return makeError(ErrorCode::Io, "could not open the file: %1", {pathToUtf8(file)});
    }
    const std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        return makeError(ErrorCode::Io, "error reading the file: %1", {pathToUtf8(file)});
    }

    auto config = parseUserConfiguration(content);
    if (!config) {
        // Добавляем имя файла, чтобы было понятно, какой именно файл испорчен.
        return wrapError(config.error(), "%1", {pathToUtf8(file)});
    }
    return config;
}

} // namespace harmony::formats
