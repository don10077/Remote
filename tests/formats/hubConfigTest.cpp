/**
 * @file hubConfigTest.cpp
 * @brief Разбор конфигурации Harmony Hub.
 *
 * Данные повторяют подлинные формы из локальных исходников: ответ engine?config —
 * как в фикстурах harmony-ng, родные файлы ресурсов (DevicesWithFeatures, Activities,
 * KeyCode) — как в фикстурах harmony-hub-control.
 */

#include "harmony/core/formats/hubConfig.h"

#include <gtest/gtest.h>

using namespace harmony;
using namespace harmony::formats;

namespace {

/// Родные файлы ресурсов рутованного хаба: в командах есть протокол и код.
constexpr std::string_view kResources = R"({
  "DeviceList.json": {"DevicesWithFeatures": [
    {"Device": {"Id-": 74521691, "Name": "Smart TV", "Manufacturer": "LG", "Model": "OLED65C4",
                "DeviceType": 1, "DeviceTypeDisplayName": "Television", "ControlPort": 7},
     "Commands": [
       {"Name": "PowerOn", "KeyCode": "G:Toshiba 32 Bit:(0x20DF3AC5)(Repeat)():3",
        "TransportType": 1, "ProtocolId": 2, "IsLearned": false},
       {"Name": "Power", "KeyCode": "G:Toshiba 32 Bit:(0x00FDC23D)(Repeat)():3",
        "TransportType": 1, "IsLearned": true},
       {"Name": "0", "KeyCode": "G:HID 16 Bit:()(07000027)():1", "TransportType": 32},
       {"Name": "Dim", "TransportType": 64}
     ]}
  ]},
  "ActivityList.json": {"Activities": [
    {"Id-": 42111479, "Name": "Smart TV", "ActivityOrder": 1, "Roles": [
       {"DeviceId-": 74521691, "PowerOffOrder": 1,
        "SelectedInput": {"Id-": 187079432, "Name": "HDMI 3"},
        "__type": "DisplayActivityRole:#Logitech.Harmony.Services"}
     ]},
    {"Id-": -1, "Name": "PowerOff", "ActivityOrder": 0, "Roles": []}
  ]}
})";

/// Наш файл чтения стокового хаба по сети: ИК-кодов в нём нет.
constexpr std::string_view kNetwork = R"({
  "format": "harmony-hub-config", "version": 1, "createdUtc": "2026-11-14T10:00:00Z",
  "remoteId": "1234567", "hubName": "Living Room", "firmware": "4.15.600",
  "config": {
    "device": [{"id": "1", "label": "Living Room TV", "manufacturer": "Samsung", "model": "UE55",
                "type": "Television", "controlGroup": [
                  {"name": "Power", "function": [{"name": "PowerOn", "label": "PowerOn",
                     "action": "{\"command\":\"PowerOn\",\"type\":\"IRCommand\",\"deviceId\":\"1\"}"}]},
                  {"name": "Volume", "function": [{"name": "VolumeUp"}]}]}],
    "activity": [
      {"id": "10", "label": "Watch TV", "type": "WatchTV", "activityOrder": 1,
       "fixit": {"1": {"id": "1", "Power": "On", "Input": "HDMI 1"}}},
      {"id": "-1", "label": "PowerOff", "type": "PowerOff", "activityOrder": 0, "fixit": {}}]
  }
})";

} // namespace

TEST(HubConfig, ParsesKeyCodeOfIrCommand) {
    HubCommand command;
    ASSERT_TRUE(parseKeyCode("G:Toshiba 32 Bit:(0x20DF3AC5)(Repeat)():3", command));
    EXPECT_EQ(command.protocol, "Toshiba 32 Bit");
    EXPECT_EQ(command.code, "0x20DF3AC5");
    EXPECT_TRUE(command.repeat);
    EXPECT_TRUE(command.hasCode());
}

TEST(HubConfig, ParsesKeyCodeOfBluetoothCommand) {
    HubCommand command;
    // У Bluetooth первая группа пустая, код — во второй.
    ASSERT_TRUE(parseKeyCode("G:HID 16 Bit:()(07000027)():1", command));
    EXPECT_EQ(command.protocol, "HID 16 Bit");
    EXPECT_EQ(command.code, "07000027");
    EXPECT_FALSE(command.repeat);
}

TEST(HubConfig, RejectsUnparsableKeyCode) {
    HubCommand command;
    EXPECT_FALSE(parseKeyCode("", command));
    EXPECT_FALSE(parseKeyCode("nonsense", command));
    EXPECT_FALSE(parseKeyCode("G:Only Protocol:", command));
    EXPECT_FALSE(parseKeyCode("G:Unclosed:(0x20DF", command));
    EXPECT_EQ(command.keyCode, "G:Unclosed:(0x20DF"); // исходная строка сохраняется всегда
}

TEST(HubConfig, ReadsResourcesWithIrCodes) {
    const auto hub = parseHubConfig(kResources);
    ASSERT_TRUE(hub) << hub.error().message;
    EXPECT_EQ(hub->source, HubConfigSource::Resources);
    ASSERT_EQ(hub->devices.size(), 1U);
    const auto& device = hub->devices[0];
    EXPECT_EQ(device.id, "74521691");
    EXPECT_EQ(device.displayName(), "Smart TV");
    EXPECT_EQ(device.manufacturer, "LG");
    EXPECT_EQ(device.type, "Television");
    EXPECT_EQ(device.typeCode, 1);
    ASSERT_EQ(device.commands.size(), 4U);
    EXPECT_EQ(device.commands[0].transport, HubTransport::Ir);
    EXPECT_EQ(device.commands[0].code, "0x20DF3AC5");
    EXPECT_TRUE(device.commands[1].learned);
    EXPECT_EQ(device.commands[2].transport, HubTransport::Bluetooth);
    EXPECT_EQ(device.commands[3].transport, HubTransport::HomeAssistant);
    EXPECT_FALSE(device.commands[3].hasCode()); // у Home Assistant кода нет
    EXPECT_EQ(device.commandsWithCodes(), 3U);
    EXPECT_EQ(hub->commandCount(), 4U);
}

TEST(HubConfig, ReadsResourceActivitiesWithRoles) {
    const auto hub = parseHubConfig(kResources);
    ASSERT_TRUE(hub) << hub.error().message;
    ASSERT_EQ(hub->activities.size(), 2U);
    const auto& activity = hub->activities[0];
    EXPECT_EQ(activity.label, "Smart TV");
    EXPECT_EQ(activity.order, 1);
    EXPECT_FALSE(activity.isPowerOff());
    ASSERT_EQ(activity.roles.size(), 1U);
    EXPECT_EQ(activity.roles[0].role, "DisplayActivityRole"); // без суффикса #Logitech…
    EXPECT_EQ(activity.roles[0].deviceId, "74521691");
    EXPECT_EQ(activity.roles[0].input, "HDMI 3");
    EXPECT_TRUE(hub->activities[1].isPowerOff());
}

TEST(HubConfig, ReadsNetworkConfigWithoutCodes) {
    const auto hub = parseHubConfig(kNetwork);
    ASSERT_TRUE(hub) << hub.error().message;
    EXPECT_EQ(hub->source, HubConfigSource::NetworkConfig);
    EXPECT_EQ(hub->hubName, "Living Room");
    EXPECT_EQ(hub->firmware, "4.15.600");
    EXPECT_EQ(hub->remoteId, "1234567");
    ASSERT_EQ(hub->devices.size(), 1U);
    EXPECT_EQ(hub->devices[0].manufacturer, "Samsung");
    EXPECT_EQ(hub->devices[0].model, "UE55");
    ASSERT_EQ(hub->devices[0].commands.size(), 2U);
    EXPECT_EQ(hub->devices[0].commands[0].name, "PowerOn");
    // Главное свойство этого источника: имена команд есть, ИК-кодов нет.
    EXPECT_EQ(hub->commandsWithCodes(), 0U);
    EXPECT_FALSE(hub->warnings.empty());
    ASSERT_EQ(hub->activities.size(), 2U);
    ASSERT_EQ(hub->activities[0].roles.size(), 1U);
    EXPECT_EQ(hub->activities[0].roles[0].deviceId, "1");
    EXPECT_EQ(hub->activities[0].roles[0].input, "HDMI 1");
}

TEST(HubConfig, ReadsSingleResourceFileAndRawEngineReply) {
    // Один DeviceList.json без активностей.
    const auto devices = parseHubConfig(
        R"({"DevicesWithFeatures":[{"Device":{"Id-":1,"Name":"TV"},"Commands":[]}]})");
    ASSERT_TRUE(devices) << devices.error().message;
    EXPECT_EQ(devices->devices.size(), 1U);
    EXPECT_TRUE(devices->activities.empty());
    EXPECT_FALSE(devices->warnings.empty());

    // Ответ engine?config без нашей обёртки.
    const auto engine = parseHubConfig(R"({"device":[{"id":"2","label":"AVR"}],"activity":[]})");
    ASSERT_TRUE(engine) << engine.error().message;
    EXPECT_EQ(engine->source, HubConfigSource::NetworkConfig);
    ASSERT_EQ(engine->devices.size(), 1U);
    EXPECT_EQ(engine->devices[0].label, "AVR");
}

TEST(HubConfig, ReadsResourcesStoredAsText) {
    // В копии Melodious вложенный файл может быть строкой с JSON.
    const auto hub = parseHubConfig(
        R"({"DeviceList.json":"{\"DevicesWithFeatures\":[{\"Device\":{\"Id-\":7,\"Name\":\"AVR\"}}]}"})");
    ASSERT_TRUE(hub) << hub.error().message;
    ASSERT_EQ(hub->devices.size(), 1U);
    EXPECT_EQ(hub->devices[0].label, "AVR");
}

TEST(HubConfig, ConvertsResourcesToEditorModel) {
    const auto hub = parseHubConfig(kResources);
    ASSERT_TRUE(hub) << hub.error().message;
    const auto config = toRemoteConfig(*hub);

    ASSERT_EQ(config.devices.size(), 1U);
    const auto& device = config.devices[0];
    EXPECT_EQ(device.id, "74521691");
    EXPECT_EQ(device.displayName(), "Smart TV");
    EXPECT_EQ(device.manufacturer, "LG");
    EXPECT_EQ(device.type, "Television");
    ASSERT_EQ(device.commands.size(), 4U);
    // ИК-код переносится как есть и не переводится в номера записей IrProto.bin.
    EXPECT_EQ(device.commands[0].transport, model::Transport::Ir);
    EXPECT_EQ(device.commands[0].protocolName, "Toshiba 32 Bit");
    EXPECT_EQ(device.commands[0].native, "G:Toshiba 32 Bit:(0x20DF3AC5)(Repeat)():3");
    EXPECT_EQ(device.commands[0].protocol, 0);
    EXPECT_TRUE(device.commands[0].code.empty());
    EXPECT_EQ(device.commands[2].transport, model::Transport::Bluetooth);
    EXPECT_EQ(device.commands[3].transport, model::Transport::HomeAssistant);
    // Вход устройства виден только из активностей хаба.
    ASSERT_EQ(device.inputs.size(), 1U);
    EXPECT_EQ(device.inputs[0], "HDMI 3");

    ASSERT_EQ(config.activities.size(), 2U);
    const auto& activity = config.activities[0];
    EXPECT_EQ(activity.label, "Smart TV");
    EXPECT_FALSE(activity.isPowerOff());
    ASSERT_EQ(activity.roles.size(), 1U);
    EXPECT_EQ(activity.roles[0].name, "DisplayActivityRole");
    EXPECT_EQ(activity.roleDevice("DisplayActivityRole"), "74521691");
    ASSERT_EQ(activity.powerOn.size(), 1U);
    EXPECT_EQ(activity.powerOn[0], "74521691");
    ASSERT_EQ(activity.inputs.size(), 1U);
    EXPECT_EQ(activity.inputs[0].second, "HDMI 3");
    EXPECT_TRUE(config.isDeviceUsed("74521691"));
    // Служебную активность редактор должен узнавать по типу.
    EXPECT_TRUE(config.activities[1].isPowerOff());
    EXPECT_EQ(config.userActivityCount(), 1U);
    EXPECT_EQ(config.commandCount(), 4U);
    // Кнопки лежат в MapList.json, который здесь не разбирается.
    EXPECT_TRUE(device.controlGroups.empty());
}

TEST(HubConfig, ConvertsNetworkConfigToEditorModel) {
    const auto hub = parseHubConfig(kNetwork);
    ASSERT_TRUE(hub) << hub.error().message;
    const auto config = toRemoteConfig(*hub);
    EXPECT_EQ(config.property("HubName"), "Living Room");
    EXPECT_EQ(config.property("LastUpdated"), "2026-11-14T10:00:00Z");
    ASSERT_EQ(config.devices.size(), 1U);
    ASSERT_EQ(config.devices[0].commands.size(), 2U);
    // Без рутинга имена команд есть, а кодов нет.
    EXPECT_TRUE(config.devices[0].commands[0].protocolName.empty());
    EXPECT_TRUE(config.devices[0].commands[0].native.empty());
    ASSERT_EQ(config.activities.size(), 2U);
    ASSERT_EQ(config.activities[0].powerOn.size(), 1U);
    EXPECT_EQ(config.activities[0].powerOn[0], "1");
    ASSERT_EQ(config.devices[0].inputs.size(), 1U);
    EXPECT_EQ(config.devices[0].inputs[0], "HDMI 1");
}

TEST(HubConfig, RejectsForeignFiles) {
    EXPECT_EQ(parseHubConfig("not json").error().code, ErrorCode::Format);
    EXPECT_EQ(parseHubConfig("[]").error().code, ErrorCode::Format);
    EXPECT_EQ(parseHubConfig(R"({"format":"harmony-hid-backup","version":1})").error().code, ErrorCode::Format);
    EXPECT_EQ(parseHubConfig(R"({"format":"harmony-hub-config","version":2,"config":{"device":[]}})").error().code,
              ErrorCode::Unsupported);
    EXPECT_EQ(parseHubConfig(R"({"DevicesWithFeatures":[]})").error().code, ErrorCode::Format);
}
