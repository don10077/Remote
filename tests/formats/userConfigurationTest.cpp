/**
 * @file userConfigurationTest.cpp
 * @brief Тесты чтения UserConfiguration.xml: синтетические примеры и реальная конфигурация.
 */

#include "harmony/core/formats/userConfiguration.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using harmony::ErrorCode;
using harmony::formats::parseUserConfiguration;
using harmony::formats::readUserConfiguration;

namespace {

/// Папка с тестовыми данными (задаётся в tests/CMakeLists.txt).
const std::filesystem::path kDataDir = HARMONY_TEST_DATA_DIR;

/// Папка с реальной (обезличенной) конфигурацией пульта.
const std::filesystem::path kSampleDir = kDataDir / "userconfig";

/// Минимальная конфигурация со всеми разбираемыми элементами.
constexpr const char* kSmallConfig = R"(<?xml version="1.0" encoding="UTF-8"?>
<Root>
  <Properties><Property name="version">1.0</Property></Properties>
  <User><Id>0</Id></User>
  <Device>
    <Id>100</Id><Type>Television</Type><Manufacturer>LG</Manufacturer><Model>55UK</Model>
    <Presentation>
      <Label>Телевизор</Label>
      <ControlGroup name="Misc">
        <Button><Label>Питание</Label><Position>3</Position><ActionId>100_PowerToggle_Hold</ActionId></Button>
        <Button><Label></Label><Icon>teletext_off</Icon><Position>4</Position><ActionId>100_Text_Press</ActionId></Button>
      </ControlGroup>
    </Presentation>
    <Commands>
      <Command><Name>PowerToggle</Name><Data><Protocol>2</Protocol><Code>0x0A</Code></Data></Command>
      <Command><Name>Text</Name><Data><Protocol>2</Protocol><Code>0x0B</Code></Data></Command>
    </Commands>
  </Device>
  <Activity>
    <Id>200</Id><Type>VirtualTelevisionN</Type>
    <Presentation><Label>Смотреть ТВ</Label></Presentation>
    <Role><Name>DEFAULT</Name><DeviceId>100</DeviceId></Role>
    <Power><On>100</On><Off>300</Off></Power>
  </Activity>
</Root>)";

} // namespace

TEST(UserConfiguration, ParsesDevice) {
    const auto config = parseUserConfiguration(kSmallConfig);
    ASSERT_TRUE(config) << config.error().message;

    ASSERT_EQ(config->devices.size(), 1U);
    const auto& device = config->devices.front();
    EXPECT_EQ(device.id, "100");
    EXPECT_EQ(device.type, "Television");
    EXPECT_EQ(device.manufacturer, "LG");
    EXPECT_EQ(device.model, "55UK");
    EXPECT_EQ(device.label, "Телевизор");
    EXPECT_EQ(config->property("version"), "1.0");
}

TEST(UserConfiguration, ParsesCommands) {
    const auto config = parseUserConfiguration(kSmallConfig);
    ASSERT_TRUE(config) << config.error().message;

    const auto& commands = config->devices.front().commands;
    ASSERT_EQ(commands.size(), 2U);
    EXPECT_EQ(commands[0].name, "PowerToggle");
    EXPECT_EQ(commands[0].protocol, 2);
    EXPECT_EQ(commands[0].code, "0x0A");
}

TEST(UserConfiguration, ParsesButtons) {
    const auto config = parseUserConfiguration(kSmallConfig);
    ASSERT_TRUE(config) << config.error().message;

    const auto& groups = config->devices.front().controlGroups;
    ASSERT_EQ(groups.size(), 1U);
    EXPECT_EQ(groups[0].name, "Misc");
    ASSERT_EQ(groups[0].buttons.size(), 2U);
    EXPECT_EQ(groups[0].buttons[0].label, "Питание");
    EXPECT_EQ(groups[0].buttons[0].position, 3);
    EXPECT_EQ(groups[0].buttons[0].actionId, "100_PowerToggle_Hold");
    EXPECT_EQ(groups[0].buttons[1].label, "");
    EXPECT_EQ(groups[0].buttons[1].icon, "teletext_off");
}

TEST(UserConfiguration, ParsesActivity) {
    const auto config = parseUserConfiguration(kSmallConfig);
    ASSERT_TRUE(config) << config.error().message;

    ASSERT_EQ(config->activities.size(), 1U);
    const auto& activity = config->activities.front();
    EXPECT_EQ(activity.id, "200");
    EXPECT_EQ(activity.label, "Смотреть ТВ");
    ASSERT_EQ(activity.roles.size(), 1U);
    EXPECT_EQ(activity.roles[0].name, "DEFAULT");
    EXPECT_EQ(activity.roles[0].deviceId, "100");
    EXPECT_EQ(activity.powerOn, std::vector<std::string>{"100"});
    EXPECT_EQ(activity.powerOff, std::vector<std::string>{"300"});
}

TEST(UserConfiguration, RejectsBrokenXml) {
    const auto config = parseUserConfiguration("<Root>\n<Device>\n</Root>");

    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::Format);
    EXPECT_NE(config.error().message.find("line 3"), std::string::npos) << config.error().message;
}

TEST(UserConfiguration, RejectsWrongRoot) {
    const auto config = parseUserConfiguration("<ActionLists/>");

    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::Format);
}

TEST(UserConfiguration, RejectsDeviceWithoutId) {
    const auto config = parseUserConfiguration("<Root><Device><Type>Television</Type></Device></Root>");

    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::Format);
}

TEST(UserConfiguration, RejectsBadButtonPosition) {
    const auto config = parseUserConfiguration(
        "<Root><Device><Id>1</Id><Presentation><ControlGroup name=\"Misc\">"
        "<Button><Label>X</Label><Position>abc</Position></Button>"
        "</ControlGroup></Presentation></Device></Root>");

    ASSERT_FALSE(config);
    EXPECT_NE(config.error().message.find("abc"), std::string::npos) << config.error().message;
}

TEST(UserConfiguration, EmptyRootGivesEmptyConfig) {
    const auto config = parseUserConfiguration("<Root/>");

    ASSERT_TRUE(config) << config.error().message;
    EXPECT_TRUE(config->devices.empty());
    EXPECT_TRUE(config->activities.empty());
}

// --- Реальная конфигурация пульта -------------------------------------------

TEST(UserConfigurationSample, ReadsWholeConfig) {
    const auto config = readUserConfiguration(kSampleDir);
    ASSERT_TRUE(config) << config.error().message;

    EXPECT_EQ(config->devices.size(), 8U);
    EXPECT_EQ(config->activities.size(), 9U);
    EXPECT_EQ(config->commandCount(), 507U);
    EXPECT_EQ(config->property("version"), "1.0");
}

TEST(UserConfigurationSample, ReadsReceiver) {
    const auto config = readUserConfiguration(kSampleDir);
    ASSERT_TRUE(config) << config.error().message;

    const auto* receiver = config->findDevice("82969357");
    ASSERT_NE(receiver, nullptr);
    EXPECT_EQ(receiver->type, "Receiver");
    EXPECT_EQ(receiver->manufacturer, "Denon");
    EXPECT_EQ(receiver->model, "AVR-X4000");
    EXPECT_EQ(receiver->label, "AV-ресивер-ЗАЛ");
    EXPECT_EQ(receiver->commands.size(), 215U);
}

TEST(UserConfigurationSample, ReadsInputsAndPowerState) {
    const auto config = readUserConfiguration(kSampleDir);
    ASSERT_TRUE(config) << config.error().message;

    const auto* tv = config->findDevice("92555930");
    ASSERT_NE(tv, nullptr);
    EXPECT_TRUE(tv->powerControlled);
    ASSERT_EQ(tv->inputs.size(), 12U);
    EXPECT_EQ(tv->inputs[5], "HDMI3");
    // PlayStation 4 включают руками: состояний у неё нет.
    EXPECT_FALSE(config->findDevice("90581251")->powerControlled);
}

TEST(UserConfigurationSample, ReadsActivity) {
    const auto config = readUserConfiguration(kSampleDir);
    ASSERT_TRUE(config) << config.error().message;

    const auto* activity = config->findActivity("79016674");
    ASSERT_NE(activity, nullptr);
    EXPECT_EQ(activity->label, "ТВ + AV");
    EXPECT_EQ(activity->roles.size(), 3U);
    EXPECT_EQ(activity->powerOn, (std::vector<std::string>{"92555930", "82969357"}));
    EXPECT_TRUE(activity->usesDevice("82969357"));

    const auto* powerOff = config->findActivity("-1");
    ASSERT_NE(powerOff, nullptr);
    EXPECT_TRUE(powerOff->isPowerOff());
}

TEST(UserConfigurationSample, EveryButtonPointsToKnownDevice) {
    const auto config = readUserConfiguration(kSampleDir);
    ASSERT_TRUE(config) << config.error().message;

    // Действие кнопки начинается с id устройства — проверяем, что модель согласована.
    for (const auto& device : config->devices) {
        for (const auto& group : device.controlGroups) {
            for (const auto& button : group.buttons) {
                const std::string deviceId = button.actionId.substr(0, button.actionId.find('_'));
                EXPECT_NE(config->findDevice(deviceId), nullptr) << button.actionId;
            }
        }
    }
}

TEST(UserConfigurationSample, ReadsHardButtonNames) {
    const auto config = readUserConfiguration(kSampleDir);
    ASSERT_TRUE(config) << config.error().message;

    // Аппаратные кнопки активности: имя кнопки пульта в атрибуте name.
    const auto* activity = config->findActivity("79016674");
    ASSERT_NE(activity, nullptr);
    const auto group = std::ranges::find(activity->controlGroups, std::string("HardButtons"),
                                         &harmony::model::ControlGroup::name);
    ASSERT_NE(group, activity->controlGroups.end());
    ASSERT_FALSE(group->buttons.empty());
    EXPECT_EQ(group->buttons.front().name, "Menu");
    EXPECT_EQ(group->buttons.front().actionId, "92555930_Menu_Hold");
}

TEST(UserConfigurationSample, AcceptsFilePathAndFolderPath) {
    const auto fromFolder = readUserConfiguration(kSampleDir);
    const auto fromFile = readUserConfiguration(kSampleDir / "UserConfiguration.xml");

    ASSERT_TRUE(fromFolder);
    ASSERT_TRUE(fromFile);
    EXPECT_EQ(fromFolder->devices.size(), fromFile->devices.size());
}

TEST(UserConfigurationSample, MissingFileIsNotFound) {
    const auto config = readUserConfiguration(kDataDir / "no-such-folder");

    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::NotFound);
}
