/**
 * @file remoteConfigTest.cpp
 * @brief Тесты поиска объектов в модели конфигурации.
 */

#include "harmony/core/model/remoteConfig.h"

#include <gtest/gtest.h>

using harmony::model::Activity;
using harmony::model::ActivityRole;
using harmony::model::Device;
using harmony::model::RemoteConfig;

namespace {

/// Небольшая конфигурация: ТВ и ресивер, одна активность использует только ТВ.
RemoteConfig makeConfig() {
    RemoteConfig config;
    config.properties = {{"version", "1.0"}, {"LastUpdated", "20240802 154354"}};

    Device tv;
    tv.id = "tv";
    tv.type = "Television";
    tv.manufacturer = "Samsung";
    tv.model = "UE55";
    tv.label = "Телевизор";
    tv.commands = {{.name = "PowerOn", .protocol = 1, .code = "0x01"},
                   {.name = "VolumeUp", .protocol = 1, .code = "0x02"}};
    config.devices.push_back(tv);

    Device receiver; // без подписи — имя берётся из производителя и модели
    receiver.id = "avr";
    receiver.manufacturer = "Yamaha";
    receiver.model = "RX-V";
    config.devices.push_back(receiver);

    Activity watchTv;
    watchTv.id = "watch-tv";
    watchTv.type = "VirtualTelevisionN";
    watchTv.label = "Смотреть ТВ";
    watchTv.roles = {ActivityRole{.name = "DEFAULT", .deviceId = "tv"}};
    watchTv.powerOn = {"tv"};
    watchTv.powerOff = {"avr"};
    config.activities.push_back(watchTv);

    Activity powerOff;
    powerOff.id = "-1";
    powerOff.type = "PowerOff";
    powerOff.label = "Выключено";
    config.activities.push_back(powerOff);
    return config;
}

} // namespace

TEST(RemoteConfig, FindsDeviceById) {
    const RemoteConfig config = makeConfig();

    const Device* device = config.findDevice("avr");

    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->manufacturer, "Yamaha");
    EXPECT_EQ(config.findDevice("missing"), nullptr);
}

TEST(RemoteConfig, MutableFindAllowsEditing) {
    RemoteConfig config = makeConfig();

    config.findDevice("tv")->model = "UE65";

    EXPECT_EQ(config.findDevice("tv")->model, "UE65");
}

TEST(RemoteConfig, FindsCommandByName) {
    const RemoteConfig config = makeConfig();
    const Device* tv = config.findDevice("tv");
    ASSERT_NE(tv, nullptr);

    const auto* command = tv->findCommand("VolumeUp");

    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->code, "0x02");
    EXPECT_EQ(tv->findCommand("Mute"), nullptr);
}

TEST(RemoteConfig, DisplayNameFallsBackToManufacturerAndModel) {
    const RemoteConfig config = makeConfig();

    EXPECT_EQ(config.findDevice("tv")->displayName(), "Телевизор");
    EXPECT_EQ(config.findDevice("avr")->displayName(), "Yamaha RX-V");
    Device onlyManufacturer;
    onlyManufacturer.manufacturer = "Sony";
    EXPECT_EQ(onlyManufacturer.displayName(), "Sony");
}

TEST(RemoteConfig, FindsActivityById) {
    const RemoteConfig config = makeConfig();

    const Activity* activity = config.findActivity("watch-tv");

    ASSERT_NE(activity, nullptr);
    EXPECT_EQ(activity->label, "Смотреть ТВ");
    EXPECT_FALSE(activity->isPowerOff());
    EXPECT_TRUE(config.findActivity("-1")->isPowerOff());
    EXPECT_EQ(config.findActivity("music"), nullptr);
}

TEST(RemoteConfig, DetectsDeviceUsageInActivities) {
    const RemoteConfig config = makeConfig();

    // Выключение устройства в активности не считается использованием.
    EXPECT_TRUE(config.isDeviceUsed("tv"));
    EXPECT_FALSE(config.isDeviceUsed("avr"));
    EXPECT_FALSE(config.isDeviceUsed("missing"));
}

TEST(RemoteConfig, ReadsPropertiesAndCountsCommands) {
    const RemoteConfig config = makeConfig();

    EXPECT_EQ(config.property("version"), "1.0");
    EXPECT_EQ(config.property("missing"), "");
    EXPECT_EQ(config.commandCount(), 2U);
}
