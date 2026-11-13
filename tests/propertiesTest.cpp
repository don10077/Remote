/**
 * @file propertiesTest.cpp
 * @brief Свойства устройств и активностей: чтение, правка, регистр логических значений.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/propertyCatalog.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace harmony;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";
constexpr const char* kTv = "92555930";

std::string valueOf(const ConfigDocument& document, PropertyScope scope, const std::string& id,
                    const std::string& name) {
    for (const auto& [key, value] : document.properties(scope, id)) {
        if (key == name) return value;
    }
    return "<absent>";
}

ConfigDocument sample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

std::string firstActivity(const ConfigDocument& document) {
    for (const auto& activity : document.model().activities) {
        if (!activity.isPowerOff()) return activity.id;
    }
    return {};
}

} // namespace

TEST(Properties, ReadsDevicePauses) {
    const auto document = sample();
    EXPECT_NE(valueOf(document, PropertyScope::DeviceTiming, kTv, "PressPreSilence"), "<absent>");
    EXPECT_EQ(valueOf(document, PropertyScope::DeviceTiming, kTv, "HoldInterKey"), "100");
}

TEST(Properties, ChangesOnlyTheProperty) {
    auto document = sample();
    const std::string before = document.toXml();
    const std::string original = valueOf(document, PropertyScope::DeviceTiming, kTv, "PressPreSilence");
    ASSERT_NE(original, "<absent>");
    ASSERT_NE(original, "333");
    ASSERT_TRUE(document.setProperty(PropertyScope::DeviceTiming, kTv, "PressPreSilence", "333"));
    EXPECT_EQ(valueOf(document, PropertyScope::DeviceTiming, kTv, "PressPreSilence"), "333");
    const std::string from = "<Property name=\"PressPreSilence\">" + original + "</Property>";
    std::string expected = before;
    const auto at = expected.find(from, expected.find("<Id>" + std::string(kTv) + "</Id>"));
    ASSERT_NE(at, std::string::npos);
    expected.replace(at, from.size(), "<Property name=\"PressPreSilence\">333</Property>");
    EXPECT_EQ(document.toXml(), expected);
    // То же значение ещё раз — файл не меняется.
    ASSERT_TRUE(document.setProperty(PropertyScope::DeviceTiming, kTv, "PressPreSilence", "333"));
    EXPECT_EQ(document.toXml(), expected);
}

TEST(Properties, BooleansUseTheRemoteCasing) {
    auto document = sample();
    ASSERT_TRUE(document.setProperty(PropertyScope::Device, kTv, "AlwaysOn", "True"));
    EXPECT_EQ(valueOf(document, PropertyScope::Device, kTv, "AlwaysOn"), "true");
    const std::string activity = firstActivity(document);
    ASSERT_FALSE(activity.empty());
    ASSERT_TRUE(document.setProperty(PropertyScope::Activity, activity, "PowerOffUnusedDevices", "false"));
    EXPECT_EQ(valueOf(document, PropertyScope::Activity, activity, "PowerOffUnusedDevices"), "False");
}

TEST(Properties, EmptyValueRemovesAndUnknownIsKept) {
    auto document = sample();
    ASSERT_TRUE(document.setProperty(PropertyScope::Device, kTv, "SomethingNew", "42"));
    EXPECT_EQ(valueOf(document, PropertyScope::Device, kTv, "SomethingNew"), "42");
    ASSERT_TRUE(document.setProperty(PropertyScope::Device, kTv, "SomethingNew", ""));
    EXPECT_EQ(valueOf(document, PropertyScope::Device, kTv, "SomethingNew"), "<absent>");
}

TEST(Properties, InvalidChangesAreRefused) {
    auto document = sample();
    const std::string before = document.toXml();
    EXPECT_EQ(document.setProperty(PropertyScope::DeviceTiming, kTv, "PressPreSilence", "fast").error().code,
              ErrorCode::Format);
    EXPECT_EQ(document.setProperty(PropertyScope::DeviceTiming, kTv, "PressPreSilence", "99999").error().code,
              ErrorCode::Format);
    EXPECT_EQ(document.setProperty(PropertyScope::Device, kTv, "IsNewDevice", "true").error().code,
              ErrorCode::Unsupported);
    EXPECT_EQ(document.setProperty(PropertyScope::Device, "00000000", "AlwaysOn", "true").error().code,
              ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before);
}

TEST(Properties, CatalogDescribesPausesAndCasing) {
    const auto* pause = findProperty(PropertyScope::DeviceTiming, "PressPreSilence");
    ASSERT_NE(pause, nullptr);
    EXPECT_EQ(pause->type, PropertyInfo::Type::Int);
    EXPECT_EQ(findProperty(PropertyScope::Device, "PowerOffUnusedDevices"), nullptr);
    EXPECT_EQ(formatPropertyValue(PropertyScope::Activity, "StopOnExit", "1"), "True");
    EXPECT_EQ(formatPropertyValue(PropertyScope::Device, "Scart", "TRUE"), "true");
    EXPECT_EQ(formatPropertyValue(PropertyScope::Device, "Unknown", "Mixed"), "Mixed");
}
