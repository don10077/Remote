/**
 * @file activityUpdateTest.cpp
 * @brief Тесты правки состава активности: устройства, роли, входы, тип, кнопки.
 *
 * Решения программы проверяются на конфигурации, собранной в коде (правило 19).
 */

#include "configFixtures.h"

#include "harmony/core/activityDraft.h"
#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using harmony::ActivityDraft;
using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

using harmony::test::kActivityId;
using harmony::test::kPlayerId;
using harmony::test::kPowerOffId;
using harmony::test::kReceiverId;
using harmony::test::kTvId;

int buttonsOf(const harmony::model::Activity& activity, const std::string& deviceId) {
    int count = 0;
    for (const auto& group : activity.controlGroups) {
        for (const auto& button : group.buttons) {
            count += button.actionId.starts_with(deviceId + "_") ? 1 : 0;
        }
    }
    return count;
}

const harmony::model::Activity& activity(const ConfigDocument& document) {
    return *document.model().findActivity(kActivityId);
}

} // namespace

TEST(ActivityUpdate, DraftOfReadsCurrentComposition) {
    const ConfigDocument document = harmony::test::configFixture();
    const ActivityDraft draft = harmony::draftOf(activity(document));

    EXPECT_EQ(draft.label, "Смотреть ТВ");
    EXPECT_EQ(draft.devices, (std::vector<std::string>{kTvId, kReceiverId}));
    EXPECT_EQ(draft.controlDevice, kTvId);
    EXPECT_EQ(draft.volumeDevice, kReceiverId);
    EXPECT_TRUE(draft.displayDevice.empty()) << "роли DISPLAY в фикстуре нет";
    ASSERT_EQ(draft.inputs.size(), 1U);
    EXPECT_EQ(draft.inputs[0], (std::pair<std::string, std::string>{kTvId, "HDMI3"}));
}

TEST(ActivityUpdate, UnchangedDraftKeepsFileIntact) {
    ConfigDocument document = harmony::test::configFixture();
    const std::string before = document.toXml();

    ASSERT_TRUE(document.updateActivity(kActivityId, harmony::draftOf(activity(document)), false));

    EXPECT_EQ(document.toXml(), before) << "правка «ничего не изменив» не трогает файл";
}

TEST(ActivityUpdate, AddsDeviceAndChangesInput) {
    ConfigDocument document = harmony::test::configFixture();
    ActivityDraft draft = harmony::draftOf(activity(document));
    draft.devices.push_back(kPlayerId);
    draft.controlDevice = kPlayerId;
    draft.inputs[0].second = "HDMI1";

    ASSERT_TRUE(document.updateActivity(kActivityId, draft, false));

    const auto& updated = activity(document);
    EXPECT_EQ(updated.powerOn, (std::vector<std::string>{kTvId, kReceiverId, kPlayerId}));
    EXPECT_EQ(updated.roleDevice("DEFAULT"), kPlayerId);
    EXPECT_EQ(updated.roleDevice("VOLUME"), kReceiverId);
    ASSERT_EQ(updated.inputs.size(), 1U);
    EXPECT_EQ(updated.inputs[0], (std::pair<std::string, std::string>{kTvId, "HDMI1"}));

    // Другое действие при запуске сохранилось и стоит после входов.
    const std::string xml = document.toXml();
    const auto kept = xml.find("<Parameter name=\"Command\">Home</Parameter>");
    const auto input = xml.find("<Parameter name=\"Value\">HDMI1</Parameter>");
    ASSERT_NE(kept, std::string::npos);
    EXPECT_LT(input, kept);
}

TEST(ActivityUpdate, RemovingDeviceDropsItsButtonsAndActions) {
    ConfigDocument document = harmony::test::configFixture();
    ActivityDraft draft = harmony::draftOf(activity(document));
    const int receiverButtons = buttonsOf(activity(document), kReceiverId);
    ASSERT_GT(receiverButtons, 0);

    draft.devices = {kTvId};
    draft.volumeDevice.clear();
    draft.inputs = {{kTvId, "HDMI3"}};
    EXPECT_EQ(harmony::buttonsLostBy(activity(document), draft), receiverButtons);

    ASSERT_TRUE(document.updateActivity(kActivityId, draft, false));

    const auto& updated = activity(document);
    EXPECT_EQ(buttonsOf(updated, kReceiverId), 0) << "кнопки убранного устройства удалены";
    EXPECT_EQ(updated.roleDevice("VOLUME"), kTvId) << "громкость — у устройства управления";
    EXPECT_EQ(updated.powerOn, (std::vector<std::string>{kTvId}));
    EXPECT_EQ(updated.powerOff, (std::vector<std::string>{kReceiverId, kPlayerId}))
        << "остальные устройства выключаются";
    EXPECT_TRUE(updated.usesDevice(kTvId));
    EXPECT_FALSE(updated.usesDevice(kReceiverId));
}

TEST(ActivityUpdate, ReassignsHardButtonsByRoles) {
    ConfigDocument document = harmony::test::configFixture();
    ActivityDraft draft = harmony::draftOf(activity(document));
    draft.volumeDevice = kTvId;

    ASSERT_TRUE(document.updateActivity(kActivityId, draft, true));

    const auto& updated = activity(document);
    const harmony::model::ControlGroup* hard = nullptr;
    for (const auto& group : updated.controlGroups) {
        if (group.name == "HardButtons") {
            hard = &group;
            break;
        }
    }
    ASSERT_NE(hard, nullptr);
    const harmony::model::Button* volume = nullptr;
    for (const auto& button : hard->buttons) {
        if (button.name == "VolumeUp") volume = &button;
    }
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->actionId, std::string(kTvId) + "_VolumeUp_Hold")
        << "аппаратная клавиша переназначена устройству громкости";
    for (const auto& button : hard->buttons) {
        EXPECT_FALSE(button.actionId.starts_with(std::string(kReceiverId) + "_")) << button.actionId;
    }
    EXPECT_EQ(buttonsOf(updated, kReceiverId), 1) << "экранные кнопки остались за своим устройством";
}

TEST(ActivityUpdate, ChangingTypeReplacesPropertiesAndChannelList) {
    ConfigDocument document = harmony::test::configFixture();
    ActivityDraft draft = harmony::draftOf(activity(document));
    draft.type = "VirtualDvd";
    draft.label = "Кино";

    ASSERT_TRUE(document.updateActivity(kActivityId, draft, false));

    const std::string xml = document.toXml();
    const auto start = xml.find("<Id>" + std::string(kActivityId) + "</Id>");
    const auto end = xml.find("</Activity>", start);
    ASSERT_NE(start, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    const std::string block = xml.substr(start, end - start);

    EXPECT_NE(block.find("<Type>VirtualDvd</Type>"), std::string::npos);
    EXPECT_NE(block.find("EnableSmartMenu"), std::string::npos) << "свойства — как у нового типа";
    EXPECT_EQ(block.find("ChannelList"), std::string::npos) << "список каналов остаётся только у ТВ";
    EXPECT_EQ(activity(document).label, "Кино");
}

TEST(ActivityUpdate, RejectsPowerOffAndBadDrafts) {
    ConfigDocument document = harmony::test::configFixture();
    const ActivityDraft draft = harmony::draftOf(activity(document));

    EXPECT_EQ(document.updateActivity(kPowerOffId, draft, false).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(document.updateActivity("нет", draft, false).error().code, ErrorCode::NotFound);

    ActivityDraft empty = draft;
    empty.devices.clear();
    EXPECT_EQ(document.updateActivity(kActivityId, empty, false).error().code, ErrorCode::Format);
    EXPECT_EQ(document.toXml(), harmony::test::kConfigXml) << "отказ ничего не изменил";
}
