/**
 * @file remotePreferencesTest.cpp
 * @brief Настройки пульта (platformconfig/system_*.dat): чтение, правка, формат часов в XML.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/zipArchive.h"
#include "harmony/core/remotePreferences.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using harmony::ConfigDocument;
using harmony::ErrorCode;
namespace formats = harmony::formats;

namespace {

const std::filesystem::path kSample = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "ezhex" / "harmony900.EZHex";

formats::Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

ConfigDocument sample() {
    auto document = ConfigDocument::fromEzHex(readFile(kSample), "sample");
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

formats::ZipArchive archiveOf(const formats::Bytes& ezhex) {
    auto container = formats::parseEzHex(ezhex);
    auto zip = formats::readZip(container->payload);
    return std::move(*zip);
}

} // namespace

TEST(RemotePreferences, EndpointAndPathFollowTheFileName) {
    const auto* level = harmony::findRemotePreference("backlight_level");
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->endpoint(), "/system/backlightlevel");
    EXPECT_EQ(level->archivePath(), "platformconfig/system_backlightlevel.dat");
    EXPECT_EQ(harmony::findRemotePreference("nothing"), nullptr);
}

TEST(RemotePreferences, ValuesOutsideTheFirmwareRangeAreRefused) {
    const auto& timeout = *harmony::findRemotePreference("backlight_timeout");
    EXPECT_TRUE(harmony::checkPreferenceValue(timeout, "10"));
    // Пульт читает только две цифры: 120 стало бы 12.
    EXPECT_FALSE(harmony::checkPreferenceValue(timeout, "120"));
    EXPECT_FALSE(harmony::checkPreferenceValue(timeout, "0"));
    EXPECT_FALSE(harmony::checkPreferenceValue(timeout, "ten"));
    const auto& sound = *harmony::findRemotePreference("sound");
    EXPECT_TRUE(harmony::checkPreferenceValue(sound, "1"));
    EXPECT_FALSE(harmony::checkPreferenceValue(sound, "yes"));
}

TEST(RemotePreferences, ChangingASettingTouchesOnlyItsFile) {
    ConfigDocument document = sample();
    const auto before = archiveOf(*document.toEzHex(false));
    ASSERT_TRUE(document.setRemotePreference("backlight_level", "33"));
    EXPECT_EQ(document.remotePreference("backlight_level"), "33");

    const auto after = archiveOf(*document.toEzHex(false));
    for (const auto& entry : before.entries) {
        if (entry.name == "platformconfig/system_backlightlevel.dat") {
            continue;
        }
        const auto* same = after.find(entry.name);
        ASSERT_NE(same, nullptr) << entry.name;
        EXPECT_EQ(same->data, entry.data) << entry.name;
    }
    const auto* level = after.find("platformconfig/system_backlightlevel.dat");
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(*formats::entryContent(*level), (formats::Bytes{'3', '3'}));
    // Для пульта — права, при которых служба настроек может его читать и писать.
    const auto forRemote = archiveOf(*document.toEzHex(true));
    EXPECT_EQ(forRemote.find("platformconfig/system_backlightlevel.dat")->unixMode(), 0100666U);
}

TEST(RemotePreferences, TimeFormatIsWrittenToTheFileAndToTheXml) {
    ConfigDocument document = sample();
    ASSERT_TRUE(document.setRemotePreference("time_format", "Civilian"));
    EXPECT_EQ(document.remotePreference("time_format"), "Civilian");
    EXPECT_NE(document.toXml().find(">Civilian<"), std::string::npos);
}

TEST(RemotePreferences, InvalidValueChangesNothing) {
    ConfigDocument document = sample();
    const auto before = *document.toEzHex(false);
    EXPECT_EQ(document.setRemotePreference("theme", "7").error().code, ErrorCode::Format);
    EXPECT_EQ(document.setRemotePreference("volume", "1").error().code, ErrorCode::NotFound);
    EXPECT_EQ(*document.toEzHex(false), before);
}

TEST(RemotePreferences, EmptyValueRemovesTheFile) {
    ConfigDocument document = sample();
    ASSERT_TRUE(document.setRemotePreference("child_lock", "1"));
    ASSERT_TRUE(document.setRemotePreference("child_lock", ""));
    EXPECT_FALSE(document.remotePreference("child_lock"));
    EXPECT_EQ(archiveOf(*document.toEzHex(false)).find("platformconfig/system_childlock.dat"), nullptr);
}

namespace {

/// Та же конфигурация с другой картой RF-бластера (как если её поменяли на пульте).
formats::Bytes withRfMap(const formats::Bytes& ezhex, const std::string& map) {
    auto container = formats::parseEzHex(ezhex);
    auto zip = formats::readZip(container->payload);
    const std::string path = "platformconfig/XmlUserRfSetting.xml";
    const formats::Bytes content(map.begin(), map.end());
    if (formats::ZipEntry* entry = zip->find(path)) {
        EXPECT_TRUE(formats::setContent(*entry, content));
    } else {
        zip->entries.push_back(formats::makeEntry(path, 0100664, content, 0));
    }
    container->payload = formats::writeZip(*zip);
    return *formats::writeEzHex(*container);
}

std::string rfMap(const std::vector<std::string>& devices) {
    std::string xml = "<RemoteInfo><Controllers><Controller><Guid>0</Guid><Label>0</Label></Controller>"
                      "<Controller><Guid>00:04:20:e0:00:08:6d:84</Guid><Label>1</Label></Controller></Controllers>"
                      "<Controller2UserDeviceMap><Controller guid=\"00:04:20:e0:00:08:6d:84\">";
    for (const auto& id : devices) {
        xml += "<Device><UserDeviceId>" + id + "</UserDeviceId><PortNumber>-1</PortNumber></Device>";
    }
    return xml + "</Controller></Controller2UserDeviceMap></RemoteInfo>";
}

} // namespace

TEST(RfBlaster, WriteKeepsTheRemotesCurrentRfSettings) {
    const auto original = readFile(kSample);
    auto document = ConfigDocument::fromEzHex(withRfMap(original, rfMap({"old"})), "open");
    auto remote = ConfigDocument::fromEzHex(withRfMap(original, rfMap({"paired-on-remote"})), "remote");
    ASSERT_TRUE(document && remote);
    ASSERT_TRUE(document->setDeviceLabel(document->model().devices.front().id, "Renamed"));

    auto written = document->toEzHexKeepingRemoteRf(*remote);
    ASSERT_TRUE(written) << written.error().message;
    const auto archive = archiveOf(*written);
    const auto* rf = archive.find("platformconfig/XmlUserRfSetting.xml");
    ASSERT_NE(rf, nullptr);
    const auto content = formats::entryContent(*rf);
    EXPECT_NE(std::string(content->begin(), content->end()).find("paired-on-remote"), std::string::npos);
    // Устройства и активности — из открытой конфигурации.
    const auto xml = formats::entryContent(*archive.find("userconfig/UserConfiguration.xml"));
    EXPECT_NE(std::string(xml->begin(), xml->end()).find("Renamed"), std::string::npos);
}

TEST(RfBlaster, CheckWarnsAboutDevicesNotOnTheBlaster) {
    auto base = ConfigDocument::fromEzHex(readFile(kSample), "sample");
    ASSERT_TRUE(base);
    const auto& devices = base->model().devices;
    ASSERT_GE(devices.size(), 2U);
    std::vector<std::string> mapped;
    for (std::size_t i = 1; i < devices.size(); ++i) {
        mapped.push_back(devices[i].id);
    }
    auto document = ConfigDocument::fromEzHex(withRfMap(readFile(kSample), rfMap(mapped)), "rf");
    ASSERT_TRUE(document);
    EXPECT_EQ(document->devicesOutsideRf(), std::vector<std::string>{devices.front().id});

    // Без бластера (только передний ИК) — предупреждать не о чем.
    auto front = ConfigDocument::fromEzHex(
        withRfMap(readFile(kSample), "<RemoteInfo><Controllers><Controller><Guid>0</Guid></Controller>"
                                     "</Controllers></RemoteInfo>"),
        "front");
    ASSERT_TRUE(front);
    EXPECT_TRUE(front->devicesOutsideRf().empty());
}

TEST(RemotePreferences, NewConfigurationGetsAnArchiveForSettings) {
    ConfigDocument document = ConfigDocument::createNew("new");
    EXPECT_FALSE(document.remotePreference("sound"));
    ASSERT_TRUE(document.setRemotePreference("sound", "1"));
    EXPECT_EQ(document.remotePreference("sound"), "1");
    const auto archive = archiveOf(*document.toEzHex(false));
    EXPECT_NE(archive.find("platformconfig/"), nullptr);
    EXPECT_NE(archive.find("platformconfig/system_sound.dat"), nullptr);
}
