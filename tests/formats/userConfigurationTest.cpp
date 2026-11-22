/**
 * @file userConfigurationTest.cpp
 * @brief Тесты чтения UserConfiguration.xml: решения программы проверяются на XML, собранном в
 *        коде (правило 19). Настоящие байты проверяет тест через `dumpFiles.h`; без `dumps/` он
 *        пропускается.

 * Числа, привязанные к содержимому конкретного файла (число устройств, id, подписи), в тесте не
 * утверждаются: они следовали из чужой конфигурации, а не из решения программы.
 */

#include "dumpFiles.h"
#include "formatFixtures.h"

#include "harmony/core/formats/userConfiguration.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using harmony::ErrorCode;
using harmony::formats::parseUserConfiguration;
using harmony::formats::readUserConfiguration;

namespace {

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
}

TEST(UserConfiguration, ParsesCommands) {
    const auto config = parseUserConfiguration(kSmallConfig);
    ASSERT_TRUE(config) << config.error().message;

    const auto& device = config->devices.front();
    ASSERT_EQ(device.commands.size(), 2U);
    EXPECT_EQ(device.commands[0].name, "PowerToggle");
    EXPECT_EQ(device.commands[0].protocol, 2);
    EXPECT_EQ(config->commandCount(), 2U);
}

TEST(UserConfiguration, ParsesButtons) {
    const auto config = parseUserConfiguration(kSmallConfig);
    ASSERT_TRUE(config) << config.error().message;

    const auto& device = config->devices.front();
    ASSERT_EQ(device.controlGroups.size(), 1U);
    const auto& group = device.controlGroups.front();
    EXPECT_EQ(group.name, "Misc");
    ASSERT_EQ(group.buttons.size(), 2U);

    EXPECT_EQ(group.buttons[0].label, "Питание");
    EXPECT_EQ(group.buttons[0].position, 3);
    EXPECT_EQ(group.buttons[0].actionId, "100_PowerToggle_Hold");
    EXPECT_TRUE(group.buttons[0].icon.empty());

    EXPECT_EQ(group.buttons[1].label, "");
    EXPECT_EQ(group.buttons[1].icon, "teletext_off");
    EXPECT_EQ(group.buttons[1].position, 4);
}

TEST(UserConfiguration, ParsesActivity) {
    const auto config = parseUserConfiguration(kSmallConfig);
    ASSERT_TRUE(config) << config.error().message;

    ASSERT_EQ(config->activities.size(), 1U);
    const auto& activity = config->activities.front();
    EXPECT_EQ(activity.id, "200");
    EXPECT_EQ(activity.type, "VirtualTelevisionN");
    EXPECT_EQ(activity.label, "Смотреть ТВ");
    ASSERT_EQ(activity.roles.size(), 1U);
    EXPECT_EQ(activity.roles[0].deviceId, "100");
    EXPECT_TRUE(activity.usesDevice("100"));
    EXPECT_FALSE(activity.usesDevice("1000"));
}

TEST(UserConfiguration, RejectsBrokenXml) {
    const auto config = parseUserConfiguration("<Root><Device>");
    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::Format);
}

TEST(UserConfiguration, RejectsWrongRoot) {
    const auto config = parseUserConfiguration("<Configuration><User><Id>0</Id></User></Configuration>");
    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::Format);
    EXPECT_EQ(config.error().message, "this is not UserConfiguration.xml: no <Root> element");
}

TEST(UserConfiguration, RejectsDeviceWithoutId) {
    const auto config = parseUserConfiguration(R"(<Root><Device><Type>Television</Type></Device></Root>)");
    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::Format);
}

TEST(UserConfiguration, RejectsBadButtonPosition) {
    const auto config = parseUserConfiguration(
        R"(<Root><Device><Id>1</Id><Presentation><ControlGroup name="Misc"><Button>
           <Label>X</Label><Position>abc</Position></Button></ControlGroup></Presentation></Device></Root>)");
    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::Format);
    EXPECT_NE(config.error().message.find("abc"), std::string::npos) << config.error().message;
}

TEST(UserConfiguration, EmptyRootGivesEmptyConfig) {
    const auto config = parseUserConfiguration("<Root></Root>");
    ASSERT_TRUE(config) << config.error().message;
    EXPECT_TRUE(config->devices.empty());
    EXPECT_TRUE(config->activities.empty());
    EXPECT_TRUE(config->properties.empty());
}

TEST(UserConfiguration, MissingFileIsNotFound) {
    const auto config = readUserConfiguration("no-such-folder");
    ASSERT_FALSE(config);
    EXPECT_EQ(config.error().code, ErrorCode::NotFound);
}

// Файл и папка с файлом — тот же результат (решение: имя файла можно не указывать).
TEST(UserConfiguration, AcceptsFilePathAndFolderPath) {
    const harmony::test::TempFolder folder("userconfig");
    harmony::test::writeFile(folder.path() / "UserConfiguration.xml", kSmallConfig);

    const auto fromFolder = readUserConfiguration(folder.path());
    const auto fromFile = readUserConfiguration(folder.path() / "UserConfiguration.xml");
    ASSERT_TRUE(fromFolder) << fromFolder.error().message;
    ASSERT_TRUE(fromFile) << fromFile.error().message;
    EXPECT_EQ(fromFolder->devices.size(), fromFile->devices.size());
    EXPECT_EQ(fromFolder->devices.front().id, fromFile->devices.front().id);
}

// Регрессия на настоящих байтах: пульт читается, и каждая кнопка ссылается на известное
// устройство. Без `dumps/` тест пропускается — это проверка только на машине владельца.
TEST(UserConfiguration, RealDumpButtonsPointToKnownDevices) {
    const auto dump = harmony::test::dumpFile("EZHex/harmony900", "*.EZHex");
    if (dump.empty()) GTEST_SKIP() << "dumps/EZHex/harmony900 нет — только на машине владельца";

    const auto archive = harmony::test::archiveOf(dump);
    const std::string xml = harmony::test::entryText(archive, "userconfig/UserConfiguration.xml");
    if (xml.empty()) GTEST_SKIP() << "в дампе нет UserConfiguration.xml";

    const auto config = parseUserConfiguration(xml);
    ASSERT_TRUE(config) << config.error().message;

    for (const auto& device : config->devices) {
        for (const auto& group : device.controlGroups) {
            for (const auto& button : group.buttons) {
                const std::string deviceId = button.actionId.substr(0, button.actionId.find('_'));
                EXPECT_NE(config->findDevice(deviceId), nullptr) << button.actionId;
            }
        }
    }
}
