/**
 * @file hubConfig.cpp
 * @brief Разбор конфигурации Harmony Hub трёх видов: см. hubConfig.h.
 *
 * Формы данных взяты из локальных исходников: ответ engine?config — из
 * совместимого режима harmony-ng (payload/source/hbus_server.c, фикстуры тестов),
 * родные файлы ресурсов — из harmony-hub-control (Melodious), где они читаются
 * и пишутся как есть (tools/webui-sim/fixtures, tools/hub-emu/make-seed.mjs).
 */

#include "harmony/core/formats/hubConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <initializer_list>

namespace harmony::formats {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kOurFormat = "harmony-hub-config";
constexpr int kOurVersion = 1;
constexpr std::size_t kMaxItems = 10000; ///< Защита от файла, собранного вручную

/// Член объекта или nullptr; не бросает исключений.
const Json* member(const Json& object, const char* key) {
    if (!object.is_object()) return nullptr;
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

/// Первый из членов, который есть в объекте (хаб называет одно и то же по-разному).
const Json* firstOf(const Json& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (const Json* value = member(object, key); value != nullptr && !value->is_null()) return value;
    }
    return nullptr;
}

/// Строка или число как текст; иначе пусто (идентификаторы бывают и числами).
std::string textOf(const Json* value) {
    if (value == nullptr) return {};
    if (value->is_string()) return value->get<std::string>();
    if (value->is_number_integer()) return std::to_string(value->get<long long>());
    if (value->is_number_unsigned()) return std::to_string(value->get<unsigned long long>());
    return {};
}

/// Целое число; значение по умолчанию, если поля нет или оно другого вида.
int numberOf(const Json* value, int fallback = 0) {
    if (value == nullptr) return fallback;
    if (value->is_number_integer() || value->is_number_unsigned()) return value->get<int>();
    if (value->is_string()) {
        try {
            return std::stoi(value->get<std::string>());
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

/// Логическое значение; хаб присылает и true/false, и строки «true»/«false».
bool flagOf(const Json* value) {
    if (value == nullptr) return false;
    if (value->is_boolean()) return value->get<bool>();
    if (value->is_string()) return value->get<std::string>() == "true";
    return false;
}

/// Массив или nullptr.
const Json* arrayOf(const Json& object, const char* key) {
    const Json* value = member(object, key);
    return value != nullptr && value->is_array() ? value : nullptr;
}

HubTransport transportOf(int code) {
    switch (code) {
    case 1: return HubTransport::Ir;
    case 32: return HubTransport::Bluetooth;
    case 64: return HubTransport::HomeAssistant;
    default: return HubTransport::Unknown;
    }
}

/// Разбирает вложенный файл: он бывает объектом, а бывает текстом JSON (копия Melodious).
Json nested(const Json& value) {
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        Json parsed = Json::parse(text.begin(), text.end(), nullptr, false);
        return parsed.is_discarded() ? Json() : parsed;
    }
    return value;
}

// --- Родные файлы ресурсов хаба -------------------------------------------------------

/// Команда из DeviceList.json: имя, канал и KeyCode с протоколом и кодом.
HubCommand parseResourceCommand(const Json& item) {
    HubCommand command;
    command.name = textOf(member(item, "Name"));
    command.transportCode = numberOf(firstOf(item, {"TransportType"}), 1);
    command.transport = transportOf(command.transportCode);
    command.learned = flagOf(member(item, "IsLearned"));
    parseKeyCode(textOf(firstOf(item, {"KeyCode", "Raw"})), command);
    return command;
}

/// Устройство из DevicesWithFeatures: описание в Device, команды рядом.
void parseResourceDevices(const Json& list, HubConfiguration& out) {
    const Json* devices = arrayOf(list, "DevicesWithFeatures");
    if (devices == nullptr) return;
    for (const Json& item : *devices) {
        if (!item.is_object() || out.devices.size() >= kMaxItems) break;
        const Json* description = member(item, "Device");
        if (description == nullptr || !description->is_object()) continue;
        HubDevice device;
        device.id = textOf(firstOf(*description, {"Id-", "Id"}));
        device.label = textOf(firstOf(*description, {"Name", "FriendlyName", "Label"}));
        device.manufacturer = textOf(member(*description, "Manufacturer"));
        device.model = textOf(member(*description, "Model"));
        device.type = textOf(firstOf(*description, {"DeviceTypeDisplayName", "Type"}));
        device.typeCode = numberOf(member(*description, "DeviceType"));
        if (const Json* commands = arrayOf(item, "Commands"); commands != nullptr) {
            for (const Json& entry : *commands) {
                if (!entry.is_object() || device.commands.size() >= kMaxItems) break;
                HubCommand command = parseResourceCommand(entry);
                if (!command.name.empty()) device.commands.push_back(std::move(command));
            }
        }
        if (!device.id.empty()) out.devices.push_back(std::move(device));
    }
}

/// Активность из ActivityList.json: имя, порядок и роли устройств с выбранными входами.
void parseResourceActivities(const Json& list, HubConfiguration& out) {
    const Json* activities = arrayOf(list, "Activities");
    if (activities == nullptr) return;
    for (const Json& item : *activities) {
        if (!item.is_object() || out.activities.size() >= kMaxItems) break;
        HubActivity activity;
        activity.id = textOf(firstOf(item, {"Id-", "Id"}));
        activity.label = textOf(firstOf(item, {"Name", "Label"}));
        activity.type = textOf(firstOf(item, {"ActivityTypeDisplayName", "Type"}));
        activity.order = numberOf(member(item, "ActivityOrder"));
        if (const Json* roles = arrayOf(item, "Roles"); roles != nullptr) {
            for (const Json& entry : *roles) {
                if (!entry.is_object() || activity.roles.size() >= kMaxItems) break;
                HubActivityRole role;
                role.role = textOf(member(entry, "__type"));
                // «DisplayActivityRole:#Logitech.Harmony…» — оставляем только имя роли.
                if (const auto colon = role.role.find(':'); colon != std::string::npos)
                    role.role.resize(colon);
                role.deviceId = textOf(firstOf(entry, {"DeviceId-", "DeviceId"}));
                if (const Json* input = member(entry, "SelectedInput");
                    input != nullptr && input->is_object())
                    role.input = textOf(member(*input, "Name"));
                if (!role.deviceId.empty()) activity.roles.push_back(std::move(role));
            }
        }
        if (!activity.id.empty()) out.activities.push_back(std::move(activity));
    }
}

/// Добавляет кнопку, если у действия есть команда устройства; иначе считает её пропущенной.
void addMapButton(const Json& button, const Json* action, bool longPress, HubButtonMap& map) {
    if (action == nullptr || !action->is_object()) return;
    if (textOf(member(*action, "__type")) != "ButtonCommandAction") {
        ++map.skipped;
        return;
    }
    HubButton item;
    item.key = textOf(member(button, "ButtonKey"));
    item.label = textOf(member(button, "TextOnRemote"));
    item.deviceId = textOf(firstOf(*action, {"DeviceId-", "DeviceId"}));
    item.command = textOf(member(*action, "CommandName"));
    item.longPress = longPress;
    if (item.deviceId.empty() || item.command.empty() || (item.key.empty() && item.label.empty())) {
        ++map.skipped;
        return;
    }
    map.buttons.push_back(std::move(item));
}

/// MapList.json: раскладки кнопок по пультам и активностям (ButtonMaps).
void parseMapList(const Json& list, HubConfiguration& out) {
    const Json* maps = arrayOf(list, "ButtonMaps");
    if (maps == nullptr) return;
    for (const Json& item : *maps) {
        if (!item.is_object() || out.buttonMaps.size() >= kMaxItems) break;
        HubButtonMap map;
        map.identifier = textOf(member(item, "ButtonMapIdentifier"));
        map.activityId = textOf(member(item, "ActivityId-"));
        if (const Json* buttons = arrayOf(item, "Buttons"); buttons != nullptr) {
            for (const Json& button : *buttons) {
                if (!button.is_object() || map.buttons.size() >= kMaxItems) break;
                addMapButton(button, member(button, "ButtonAction"), false, map);
                addMapButton(button, member(button, "ButtonLongPressAction"), true, map);
            }
        }
        out.buttonMaps.push_back(std::move(map));
    }
}

// --- Ответ engine?config стокового хаба -----------------------------------------------

/// Команды устройства или активности: группы controlGroup с массивами function.
void parseNetworkFunctions(const Json& owner, std::vector<HubCommand>& commands) {
    const Json* groups = arrayOf(owner, "controlGroup");
    if (groups == nullptr) return;
    for (const Json& group : *groups) {
        const Json* functions = group.is_object() ? arrayOf(group, "function") : nullptr;
        if (functions == nullptr) continue;
        for (const Json& function : *functions) {
            if (!function.is_object() || commands.size() >= kMaxItems) break;
            HubCommand command;
            command.name = textOf(firstOf(function, {"name", "label"}));
            // ИК-кода здесь нет: action описывает только адресата команды.
            command.transport = HubTransport::Unknown;
            if (!command.name.empty()) commands.push_back(std::move(command));
        }
    }
}

void parseNetworkDevices(const Json& config, HubConfiguration& out) {
    const Json* devices = arrayOf(config, "device");
    if (devices == nullptr) return;
    for (const Json& item : *devices) {
        if (!item.is_object() || out.devices.size() >= kMaxItems) break;
        HubDevice device;
        device.id = textOf(member(item, "id"));
        device.label = textOf(member(item, "label"));
        device.manufacturer = textOf(member(item, "manufacturer"));
        device.model = textOf(member(item, "model"));
        device.type = textOf(firstOf(item, {"deviceTypeDisplayName", "type"}));
        parseNetworkFunctions(item, device.commands);
        if (!device.id.empty()) out.devices.push_back(std::move(device));
    }
}

void parseNetworkActivities(const Json& config, HubConfiguration& out) {
    const Json* activities = arrayOf(config, "activity");
    if (activities == nullptr) return;
    for (const Json& item : *activities) {
        if (!item.is_object() || out.activities.size() >= kMaxItems) break;
        HubActivity activity;
        activity.id = textOf(member(item, "id"));
        activity.label = textOf(member(item, "label"));
        activity.type = textOf(member(item, "type"));
        activity.order = numberOf(member(item, "activityOrder"));
        // Устройства активности перечислены в fixit: ключ — устройство, внутри вход и питание.
        if (const Json* fixit = member(item, "fixit"); fixit != nullptr && fixit->is_object()) {
            for (const auto& [deviceId, state] : fixit->items()) {
                if (activity.roles.size() >= kMaxItems) break;
                HubActivityRole role;
                role.deviceId = deviceId;
                if (state.is_object()) role.input = textOf(member(state, "Input"));
                activity.roles.push_back(std::move(role));
            }
        }
        if (!activity.id.empty()) out.activities.push_back(std::move(activity));
    }
}

/// Похож ли объект на ответ engine?config.
bool looksLikeNetworkConfig(const Json& value) {
    return value.is_object() && (arrayOf(value, "device") != nullptr || arrayOf(value, "activity") != nullptr);
}

} // namespace

// --- KeyCode ---------------------------------------------------------------------------

bool parseKeyCode(std::string_view keyCode, HubCommand& command) {
    command.keyCode = std::string(keyCode);
    if (keyCode.empty()) return false;
    const auto firstGroup = keyCode.find('(');
    if (firstGroup == std::string_view::npos) return false;

    // До первой скобки: «<вид>:<протокол>:». Вид («G» — из каталога, «L» — выучено) не нужен.
    std::string_view head = keyCode.substr(0, firstGroup);
    while (!head.empty() && head.back() == ':') head.remove_suffix(1);
    const auto afterKind = head.find(':');
    if (afterKind == std::string_view::npos) return false;
    command.protocol = std::string(head.substr(afterKind + 1));

    // Группы в скобках: код — первая непустая, признак повтора — отдельная группа.
    std::size_t position = firstGroup;
    while (position < keyCode.size() && keyCode[position] == '(') {
        const auto end = keyCode.find(')', position);
        if (end == std::string_view::npos) return false;
        const std::string_view group = keyCode.substr(position + 1, end - position - 1);
        if (group == "Repeat") {
            command.repeat = true;
        } else if (!group.empty() && command.code.empty()) {
            command.code = std::string(group);
        }
        position = end + 1;
    }
    return !command.protocol.empty() && !command.code.empty();
}

// --- Сводные величины ------------------------------------------------------------------

std::size_t HubDevice::commandsWithCodes() const {
    return static_cast<std::size_t>(std::ranges::count_if(commands, [](const HubCommand& c) { return c.hasCode(); }));
}

std::string HubDevice::displayName() const {
    if (!label.empty()) return label;
    if (manufacturer.empty()) return model;
    return model.empty() ? manufacturer : manufacturer + " " + model;
}

std::size_t HubConfiguration::commandCount() const {
    std::size_t total = 0;
    for (const auto& device : devices) total += device.commands.size();
    return total;
}

std::size_t HubConfiguration::commandsWithCodes() const {
    std::size_t total = 0;
    for (const auto& device : devices) total += device.commandsWithCodes();
    return total;
}

// --- Модель редактора ------------------------------------------------------------------

namespace {

/// Канал хаба в нейтральный канал модели.
model::Transport modelTransport(HubTransport transport) {
    switch (transport) {
    case HubTransport::Ir: return model::Transport::Ir;
    case HubTransport::Bluetooth: return model::Transport::Bluetooth;
    case HubTransport::HomeAssistant: return model::Transport::HomeAssistant;
    default: return model::Transport::Unknown;
    }
}

/// Добавляет значение, если его ещё нет (входы устройства в порядке появления).
void addOnce(std::vector<std::string>& values, const std::string& value) {
    if (value.empty() || std::ranges::find(values, value) != values.end()) return;
    values.push_back(value);
}

/**
 * @brief Кнопки активности из раскладки MapList.json.
 *
 * У каждого пульта своя раскладка активности; берётся та, где больше кнопок с командами
 * (при равенстве — первая в файле). Долгие нажатия не переносятся: у кнопки редактора одно
 * действие. Аппаратные клавиши и экранные кнопки идут в разные группы.
 */
std::vector<model::ControlGroup> buttonGroups(const HubConfiguration& hub, const std::string& activityId) {
    const HubButtonMap* best = nullptr;
    for (const HubButtonMap& map : hub.buttonMaps) {
        if (map.activityId != activityId) continue;
        if (best == nullptr || map.buttons.size() > best->buttons.size()) best = &map;
    }
    std::vector<model::ControlGroup> groups;
    if (best == nullptr) return groups;
    model::ControlGroup hard;
    hard.name = "HardKeys";
    model::ControlGroup soft;
    soft.name = "Screen";
    for (const HubButton& button : best->buttons) {
        if (button.longPress) continue;
        model::ControlGroup& group = button.key.empty() ? soft : hard;
        model::Button entry;
        entry.name = button.key;
        entry.label = button.label.empty() ? button.command : button.label;
        entry.position = static_cast<int>(group.buttons.size());
        entry.actionId = button.deviceId + "_" + button.command + "_Press";
        group.buttons.push_back(std::move(entry));
    }
    if (!hard.buttons.empty()) groups.push_back(std::move(hard));
    if (!soft.buttons.empty()) groups.push_back(std::move(soft));
    return groups;
}

} // namespace

model::RemoteConfig toRemoteConfig(const HubConfiguration& hub) {
    model::RemoteConfig config;
    if (!hub.createdUtc.empty()) config.properties.emplace_back("LastUpdated", hub.createdUtc);
    if (!hub.hubName.empty()) config.properties.emplace_back("HubName", hub.hubName);
    if (!hub.firmware.empty()) config.properties.emplace_back("HubFirmware", hub.firmware);

    for (const auto& device : hub.devices) {
        model::Device item;
        item.id = device.id;
        item.label = device.label;
        item.manufacturer = device.manufacturer;
        item.model = device.model;
        item.type = device.type;
        for (const auto& command : device.commands) {
            model::Command entry;
            entry.name = command.name;
            entry.transport = modelTransport(command.transport);
            entry.protocolName = command.protocol;
            entry.native = command.keyCode;
            item.commands.push_back(std::move(entry));
        }
        config.devices.push_back(std::move(item));
    }

    for (const auto& activity : hub.activities) {
        model::Activity item;
        item.id = activity.id;
        item.label = activity.label;
        // Служебную активность редактор узнаёт по типу, а хаб помечает её идентификатором «-1».
        item.type = activity.isPowerOff() ? "PowerOff" : activity.type;
        for (const auto& role : activity.roles) {
            if (!role.role.empty()) item.roles.push_back({role.role, role.deviceId});
            addOnce(item.powerOn, role.deviceId);
            if (!role.input.empty()) {
                item.inputs.emplace_back(role.deviceId, role.input);
                // Вход, который умеет устройство, виден только из активностей хаба.
                if (model::Device* device = config.findDevice(role.deviceId); device != nullptr)
                    addOnce(device->inputs, role.input);
            }
        }
        item.controlGroups = buttonGroups(hub, activity.id);
        config.activities.push_back(std::move(item));
    }
    return config;
}

// --- Разбор файла ----------------------------------------------------------------------

Result<HubConfiguration> parseHubConfig(std::string_view json) {
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return makeError(ErrorCode::Format, "this is not a Harmony Hub configuration file");

    HubConfiguration out;

    // 1. Наш файл чтения по сети: обёртка с config внутри.
    if (textOf(member(root, "format")) == kOurFormat) {
        if (const Json* version = member(root, "version");
            version == nullptr || !version->is_number_integer() || version->get<int>() != kOurVersion)
            return makeError(ErrorCode::Unsupported, "unsupported Harmony Hub configuration file version %1",
                             {version != nullptr ? version->dump() : std::string("?")});
        const Json* config = member(root, "config");
        if (config == nullptr || !looksLikeNetworkConfig(*config))
            return makeError(ErrorCode::Format, "the Harmony Hub configuration file has no configuration");
        out.source = HubConfigSource::NetworkConfig;
        out.hubName = textOf(member(root, "hubName"));
        out.firmware = textOf(member(root, "firmware"));
        out.remoteId = textOf(member(root, "remoteId"));
        out.createdUtc = textOf(member(root, "createdUtc"));
        parseNetworkDevices(*config, out);
        parseNetworkActivities(*config, out);
        out.warnings.emplace_back("the stock Hub does not send IR codes over the network: command names only");
        return out;
    }

    // 2. Родные файлы ресурсов: по отдельности, вместе в копии Melodious или в объекте
    //    с ключами deviceList/activityList.
    const Json* deviceList = nullptr;
    const Json* activityList = nullptr;
    const Json* mapList = nullptr;
    Json unpackedDevices;
    Json unpackedActivities;
    Json unpackedMaps;
    if (member(root, "DevicesWithFeatures") != nullptr) {
        deviceList = &root;
    } else if (member(root, "Activities") != nullptr) {
        activityList = &root;
    } else if (member(root, "ButtonMaps") != nullptr) {
        mapList = &root;
    } else {
        if (const Json* value = firstOf(root, {"MapList.json", "mapList"}); value != nullptr) {
            unpackedMaps = nested(*value);
            if (unpackedMaps.is_object()) mapList = &unpackedMaps;
        }
        if (const Json* value = firstOf(root, {"DeviceList.json", "deviceList"}); value != nullptr) {
            unpackedDevices = nested(*value);
            if (unpackedDevices.is_object()) deviceList = &unpackedDevices;
        }
        if (const Json* value = firstOf(root, {"ActivityList.json", "activityList"}); value != nullptr) {
            unpackedActivities = nested(*value);
            if (unpackedActivities.is_object()) activityList = &unpackedActivities;
        }
    }
    if (deviceList != nullptr || activityList != nullptr || mapList != nullptr) {
        out.source = HubConfigSource::Resources;
        if (deviceList != nullptr) parseResourceDevices(*deviceList, out);
        if (activityList != nullptr) parseResourceActivities(*activityList, out);
        if (mapList != nullptr) parseMapList(*mapList, out);
        if (deviceList == nullptr && mapList == nullptr)
            out.warnings.emplace_back("DeviceList.json is missing: activities only, without devices or IR codes");
        if (activityList == nullptr && mapList == nullptr)
            out.warnings.emplace_back("ActivityList.json is missing: devices only, without activities");
        // Файл с одним MapList.json (даже пустым) принимается: это допустимый файл ресурсов.
        if (mapList == nullptr && out.devices.empty() && out.activities.empty())
            return makeError(ErrorCode::Format, "the Harmony Hub resource file has no devices or activities");
        return out;
    }

    // 3. Ответ engine?config как есть, без нашей обёртки.
    if (looksLikeNetworkConfig(root)) {
        out.source = HubConfigSource::NetworkConfig;
        parseNetworkDevices(root, out);
        parseNetworkActivities(root, out);
        out.warnings.emplace_back("the stock Hub does not send IR codes over the network: command names only");
        return out;
    }
    return makeError(ErrorCode::Format, "this is not a Harmony Hub configuration file");
}

} // namespace harmony::formats
