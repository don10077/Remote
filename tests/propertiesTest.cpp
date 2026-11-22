/**
 * @file propertiesTest.cpp
 * @brief Свойства устройств и активностей: чтение, правка, регистр логических значений.
 *
 * Решения программы проверяются на конфигурации, собранной в коде (правило 19).
 */

#include "configFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/propertyCatalog.h"

#include <gtest/gtest.h>

using namespace harmony;

namespace {

using harmony::test::kActivityId;
using harmony::test::kTvId;

std::string valueOf(const ConfigDocument& document, PropertyScope scope, const std::string& id,
                    const std::string& name) {
    for (const auto& [key, value] : document.properties(scope, id)) {
        if (key == name) return value;
    }
    return "<absent>";
}

} // namespace

TEST(Properties, ReadsDevicePauses) {
    const auto document = harmony::test::configFixture();

    // Значения взяты из фикстуры, поэтому ожидание следует из её конструкции.
    EXPECT_EQ(valueOf(document, PropertyScope::DeviceTiming, kTvId, "PressPreSilence"), "70");
    EXPECT_EQ(valueOf(document, PropertyScope::DeviceTiming, kTvId, "HoldInterKey"), "100");
    EXPECT_EQ(valueOf(document, PropertyScope::DeviceTiming, harmony::test::kReceiverId, "PressPreSilence"),
              "<absent>");
}

TEST(Properties, ChangesOnlyTheProperty) {
    auto document = harmony::test::configFixture();
    const std::string before = document.toXml();
    const std::string original = valueOf(document, PropertyScope::DeviceTiming, kTvId, "PressPreSilence");
    ASSERT_NE(original, "<absent>");
    ASSERT_NE(original, "333");

    ASSERT_TRUE(document.setProperty(PropertyScope::DeviceTiming, kTvId, "PressPreSilence", "333"));
    EXPECT_EQ(valueOf(document, PropertyScope::DeviceTiming, kTvId, "PressPreSilence"), "333");

    const std::string from = "<Property name=\"PressPreSilence\">" + original + "</Property>";
    std::string expected = before;
    const auto at = expected.find(from, expected.find("<Id>" + std::string(kTvId) + "</Id>"));
    ASSERT_NE(at, std::string::npos);
    expected.replace(at, from.size(), "<Property name=\"PressPreSilence\">333</Property>");
    EXPECT_EQ(document.toXml(), expected) << "изменилась только одна строка";

    // То же значение ещё раз — файл не меняется.
    ASSERT_TRUE(document.setProperty(PropertyScope::DeviceTiming, kTvId, "PressPreSilence", "333"));
    EXPECT_EQ(document.toXml(), expected);
}

TEST(Properties, BooleansUseTheRemoteCasing) {
    auto document = harmony::test::configFixture();

    ASSERT_TRUE(document.setProperty(PropertyScope::Device, kTvId, "AlwaysOn", "True"));
    EXPECT_EQ(valueOf(document, PropertyScope::Device, kTvId, "AlwaysOn"), "true");

    ASSERT_TRUE(document.setProperty(PropertyScope::Activity, kActivityId, "PowerOffUnusedDevices", "false"));
    EXPECT_EQ(valueOf(document, PropertyScope::Activity, kActivityId, "PowerOffUnusedDevices"), "False");
}

TEST(Properties, EmptyValueRemovesAndUnknownIsKept) {
    auto document = harmony::test::configFixture();

    ASSERT_TRUE(document.setProperty(PropertyScope::Device, kTvId, "SomethingNew", "42"));
    EXPECT_EQ(valueOf(document, PropertyScope::Device, kTvId, "SomethingNew"), "42");
    ASSERT_TRUE(document.setProperty(PropertyScope::Device, kTvId, "SomethingNew", ""));
    EXPECT_EQ(valueOf(document, PropertyScope::Device, kTvId, "SomethingNew"), "<absent>");
}

TEST(Properties, InvalidChangesAreRefused) {
    auto document = harmony::test::configFixture();
    const std::string before = document.toXml();

    EXPECT_EQ(document.setProperty(PropertyScope::DeviceTiming, kTvId, "PressPreSilence", "fast").error().code,
              ErrorCode::Format);
    EXPECT_EQ(document.setProperty(PropertyScope::DeviceTiming, kTvId, "PressPreSilence", "99999").error().code,
              ErrorCode::Format);
    EXPECT_EQ(document.setProperty(PropertyScope::Device, kTvId, "IsNewDevice", "true").error().code,
              ErrorCode::Unsupported) << "свойство ставит сам пульт";
    EXPECT_EQ(document.setProperty(PropertyScope::Device, "00000000", "AlwaysOn", "true").error().code,
              ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before) << "отказ ничего не изменил";
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
