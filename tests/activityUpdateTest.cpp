/**
 * @file activityUpdateTest.cpp
 * @brief Тесты правки состава активности: устройства, роли, входы, тип, кнопки.
 */

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using harmony::ActivityDraft;
using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

const std::filesystem::path kSampleDir = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "userconfig";

constexpr const char* kWatchTv = "79016674"; ///< «ТВ + AV»: Hisense + ресивер, входы HDMI3 и TV Audio
constexpr const char* kTv = "92555930";
constexpr const char* kReceiver = "82969357";
constexpr const char* kDune = "68380504";

ConfigDocument openSample() {
    auto document = ConfigDocument::open(kSampleDir);
    EXPECT_TRUE(document) << document.error().message;
    return std::move(*document);
}

const harmony::model::Activity& watchTv(const ConfigDocument& document) {
    return *document.model().findActivity(kWatchTv);
}

int buttonsOf(const harmony::model::Activity& activity, const std::string& deviceId) {
    int count = 0;
    for (const auto& group : activity.controlGroups) {
        for (const auto& button : group.buttons) {
            count += button.actionId.starts_with(deviceId + "_") ? 1 : 0;
        }
    }
    return count;
}

} // namespace

TEST(ActivityUpdate, DraftOfReadsCurrentComposition) {
    const ConfigDocument document = openSample();

    const ActivityDraft draft = harmony::draftOf(watchTv(document));

    EXPECT_EQ(draft.label, "ТВ + AV");
    EXPECT_EQ(draft.devices, (std::vector<std::string>{kTv, kReceiver}));
    EXPECT_EQ(draft.controlDevice, kTv);
    EXPECT_EQ(draft.volumeDevice, kReceiver);
    EXPECT_EQ(draft.displayDevice, kTv);
    ASSERT_EQ(draft.inputs.size(), 2U);
    EXPECT_EQ(draft.inputs[0], (std::pair<std::string, std::string>{kTv, "HDMI3"}));
}

TEST(ActivityUpdate, UnchangedDraftKeepsFileIntact) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();

    ASSERT_TRUE(document.updateActivity(kWatchTv, harmony::draftOf(watchTv(document)), false));

    EXPECT_EQ(document.toXml(), before); // правка «ничего не изменив» не трогает файл
}

TEST(ActivityUpdate, AddsDeviceAndChangesInput) {
    ConfigDocument document = openSample();
    ActivityDraft draft = harmony::draftOf(watchTv(document));
    draft.devices.push_back(kDune);
    draft.controlDevice = kDune;
    draft.inputs[0].second = "HDMI1";

    ASSERT_TRUE(document.updateActivity(kWatchTv, draft, false));

    const auto& activity = watchTv(document);
    EXPECT_EQ(activity.powerOn.size(), 3U);
    EXPECT_EQ(std::ranges::count(activity.powerOff, std::string(kDune)), 0);
    EXPECT_EQ(activity.roleDevice("DEFAULT"), kDune);
    EXPECT_EQ(activity.inputs[0].second, "HDMI1");
    // Остальные действия при запуске (команда Home телевизору) сохранились — после входов.
    const std::string xml = document.toXml();
    const auto home = xml.find("<Parameter name=\"Command\">Home</Parameter>");
    const auto input = xml.find("<Parameter name=\"Value\">HDMI1</Parameter>");
    ASSERT_NE(home, std::string::npos);
    EXPECT_LT(input, home);
}

TEST(ActivityUpdate, RemovingDeviceDropsItsButtonsAndActions) {
    ConfigDocument document = openSample();
    ActivityDraft draft = harmony::draftOf(watchTv(document));
    ASSERT_GT(buttonsOf(watchTv(document), kReceiver), 0);
    draft.devices = {kTv};
    draft.volumeDevice.clear();
    draft.inputs = {{kTv, "HDMI3"}};
    EXPECT_EQ(harmony::buttonsLostBy(watchTv(document), draft), buttonsOf(watchTv(document), kReceiver));

    ASSERT_TRUE(document.updateActivity(kWatchTv, draft, false));

    const auto& activity = watchTv(document);
    EXPECT_EQ(buttonsOf(activity, kReceiver), 0);
    EXPECT_EQ(activity.roleDevice("VOLUME"), kTv); // громкость — у управления
    EXPECT_NE(std::ranges::find(activity.powerOff, std::string(kReceiver)), activity.powerOff.end());
    ASSERT_EQ(activity.inputs.size(), 1U);
}

TEST(ActivityUpdate, ReassignsHardButtonsByRoles) {
    ConfigDocument document = openSample();
    ActivityDraft draft = harmony::draftOf(watchTv(document));
    draft.volumeDevice = kTv;

    ASSERT_TRUE(document.updateActivity(kWatchTv, draft, true));

    for (const auto& group : watchTv(document).controlGroups) {
        for (const auto& button : group.buttons) {
            if (button.name == "VolumeUp") {
                EXPECT_EQ(button.actionId, std::string(kTv) + "_VolumeUp_Hold");
            }
        }
    }
    EXPECT_EQ(buttonsOf(watchTv(document), kReceiver), 1); // экранная кнопка ресивера осталась
}

TEST(ActivityUpdate, ChangingTypeReplacesPropertiesAndChannelList) {
    ConfigDocument document = openSample();
    ActivityDraft draft = harmony::draftOf(watchTv(document));
    draft.type = "VirtualDvd";
    draft.label = "Кино";

    ASSERT_TRUE(document.updateActivity(kWatchTv, draft, false));

    const std::string xml = document.toXml();
    const auto start = xml.find("<Id>79016674</Id>");
    const auto end = xml.find("</Activity>", start);
    const std::string activity = xml.substr(start, end - start);
    EXPECT_NE(activity.find("<Type>VirtualDvd</Type>"), std::string::npos);
    EXPECT_NE(activity.find("EnableSmartMenu"), std::string::npos);
    EXPECT_EQ(activity.find("ChannelList"), std::string::npos);
    EXPECT_EQ(watchTv(document).label, "Кино");
}

TEST(ActivityUpdate, RejectsPowerOffAndBadDrafts) {
    ConfigDocument document = openSample();
    ActivityDraft draft = harmony::draftOf(watchTv(document));

    EXPECT_EQ(document.updateActivity("-1", draft, false).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(document.updateActivity("нет", draft, false).error().code, ErrorCode::NotFound);
    draft.devices.clear();
    EXPECT_EQ(document.updateActivity(kWatchTv, draft, false).error().code, ErrorCode::Format);
}
