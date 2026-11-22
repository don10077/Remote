/**
 * @file configButtonsTest.cpp
 * @brief Тесты правки кнопок активности: аппаратные, экранные, перенос, отмена.
 */

#include "configFixtures.h"

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <string>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

constexpr const char* kWatchTv = harmony::test::kActivityId; ///< Телевизор + ресивер
constexpr const char* kTv = harmony::test::kTvId;
constexpr const char* kReceiver = harmony::test::kReceiverId;
constexpr const char* kDune = harmony::test::kPlayerId; ///< Не включается в активность
const harmony::ButtonOwner kOwner = harmony::ButtonOwner::activity(kWatchTv);

ConfigDocument openSample() {
    return harmony::test::configFixture();
}

/// Действие кнопки в активности: аппаратной (по имени) или экранной (по месту); пусто — нет.
std::string actionOf(const ConfigDocument& document, const std::string& key, int position = -1) {
    for (const auto& group : document.model().findActivity(kWatchTv)->controlGroups) {
        for (const auto& button : group.buttons) {
            if ((!key.empty() && button.name == key) ||
                (key.empty() && button.name.empty() && button.position == position)) {
                return button.actionId;
            }
        }
    }
    return {};
}

const harmony::model::Button* softAt(const ConfigDocument& document, int position) {
    for (const auto& group : document.model().findActivity(kWatchTv)->controlGroups) {
        for (const auto& button : group.buttons) {
            if (button.name.empty() && button.position == position) {
                return &button;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST(ActivityButtons, ReassignsHardButton) {
    ConfigDocument document = openSample();
    ASSERT_EQ(actionOf(document, "VolumeUp"), std::string(kReceiver) + "_VolumeUp_Hold");

    ASSERT_TRUE(document.setHardButton(kOwner, "VolumeUp", kTv, "Home"));

    EXPECT_EQ(actionOf(document, "VolumeUp"), std::string(kTv) + "_Home_Hold");
}

TEST(ActivityButtons, ExperimentalProjectSavesAssignmentsButCannotBeWritten) {
    ConfigDocument document = openSample();
    ASSERT_TRUE(document.setTargetRemote("1000"));
    ASSERT_TRUE(document.setHardButton(kOwner, "CustomModelKey", kReceiver, "Mute"));
    const auto saved = document.toEzHex(false);
    ASSERT_TRUE(saved);
    const auto reopened = ConfigDocument::fromEzHex(*saved, "experimental project");
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->targetRemote(), "1000");
    EXPECT_EQ(actionOf(*reopened, "CustomModelKey"), std::string(kReceiver) + "_Mute_Hold");
    EXPECT_FALSE(reopened->toEzHex(true));
}

TEST(ActivityButtons, AssignsFreeKeyAndClearsIt) {
    ConfigDocument document = openSample();
    ASSERT_TRUE(actionOf(document, "Record").empty());

    ASSERT_TRUE(document.setHardButton(kOwner, "Record", kTv, "Home"));
    EXPECT_EQ(actionOf(document, "Record"), std::string(kTv) + "_Home_Hold");

    ASSERT_TRUE(document.setHardButton(kOwner, "Record", {}, {}));
    EXPECT_TRUE(actionOf(document, "Record").empty());
}

TEST(ActivityButtons, RejectsDeviceOutsideActivityAndUnknownCommand) {
    ConfigDocument document = openSample();
    const std::string before = document.toXml();

    EXPECT_EQ(document.setHardButton(kOwner, "VolumeUp", kDune, "PowerToggle").error().code, ErrorCode::Format);
    EXPECT_EQ(document.setHardButton(kOwner, "VolumeUp", kTv, "НетТакой").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.setHardButton(harmony::ButtonOwner::activity("нет"), "VolumeUp", kTv, "Home").error().code, ErrorCode::NotFound);
    EXPECT_EQ(document.toXml(), before);
}

TEST(ActivityButtons, MovesAndCopiesHardButton) {
    ConfigDocument document = openSample();
    const std::string menu = actionOf(document, "VolumeUp");

    ASSERT_TRUE(document.moveHardButton(kOwner, "VolumeUp", "Record", true));
    EXPECT_EQ(actionOf(document, "Record"), menu);
    EXPECT_EQ(actionOf(document, "VolumeUp"), menu);

    ASSERT_TRUE(document.moveHardButton(kOwner, "VolumeUp", "Stop", false));
    EXPECT_EQ(actionOf(document, "Stop"), menu);
    EXPECT_TRUE(actionOf(document, "VolumeUp").empty());

    EXPECT_EQ(document.moveHardButton(kOwner, "VolumeUp", "Stop", false).error().code, ErrorCode::NotFound);
}

TEST(ActivityButtons, MoveOntoAssignedKeySwaps) {
    ConfigDocument document = openSample();
    const std::string menu = actionOf(document, "VolumeUp");
    ASSERT_TRUE(document.setHardButton(kOwner, "Record", kTv, "Text"));
    const std::string record = actionOf(document, "Record");

    // Как у экранных: назначение занятой кнопки не теряется, а переходит на освободившуюся.
    ASSERT_TRUE(document.moveHardButton(kOwner, "VolumeUp", "Record", false));
    EXPECT_EQ(actionOf(document, "Record"), menu);
    EXPECT_EQ(actionOf(document, "VolumeUp"), record);

    // Копия по-прежнему заменяет назначение.
    ASSERT_TRUE(document.moveHardButton(kOwner, "Record", "VolumeUp", true));
    EXPECT_EQ(actionOf(document, "VolumeUp"), menu);
}

TEST(ActivityButtons, AddsSoftButtonOnFreePlace) {
    ConfigDocument document = openSample();
    const int position = document.freeSoftPosition(kOwner);
    EXPECT_EQ(position, 0); // в фикстуре заняты 1, 2 и 4

    ASSERT_TRUE(document.setSoftButton(kOwner, position, "Сон", kTv, "Text"));

    const auto* button = softAt(document, position);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->label, "Сон");
    EXPECT_EQ(button->actionId, std::string(kTv) + "_Text_Hold");
    EXPECT_EQ(document.freeSoftPosition(kOwner), 3);
}

TEST(ActivityButtons, LabelReplacesIconButEmptyLabelKeepsIt) {
    ConfigDocument document = harmony::test::openFixture(harmony::test::replaceIn(
        harmony::test::kConfigXml, "Activity", kWatchTv,
        "<Label>Питание</Label>", "<Label/><Icon>power</Icon>"));
    const auto* before = softAt(document, 1);
    ASSERT_NE(before, nullptr);
    ASSERT_FALSE(before->icon.empty());
    const std::string icon = before->icon;

    ASSERT_TRUE(document.setSoftButton(kOwner, 1, {}, kTv, "Home"));
    EXPECT_EQ(softAt(document, 1)->icon, icon); // без подписи значок остаётся
    EXPECT_EQ(softAt(document, 1)->actionId, std::string(kTv) + "_Home_Hold");

    ASSERT_TRUE(document.setSoftButton(kOwner, 1, "Меню", kTv, "Home"));
    EXPECT_TRUE(softAt(document, 1)->icon.empty());
    EXPECT_EQ(softAt(document, 1)->label, "Меню");
}

TEST(ActivityButtons, MovesSwapsAndRemovesSoftButtons) {
    ConfigDocument document = openSample();
    const std::string first = actionOf(document, {}, 1);
    const std::string second = actionOf(document, {}, 2);

    ASSERT_TRUE(document.moveSoftButton(kOwner, 1, 2)); // занято — обмен
    EXPECT_EQ(actionOf(document, {}, 2), first);
    EXPECT_EQ(actionOf(document, {}, 1), second);

    ASSERT_TRUE(document.moveSoftButton(kOwner, 2, 13)); // на свободное место другой страницы
    EXPECT_EQ(actionOf(document, {}, 13), first);
    EXPECT_EQ(softAt(document, 2), nullptr);

    ASSERT_TRUE(document.removeSoftButton(kOwner, 13));
    EXPECT_EQ(softAt(document, 13), nullptr);
    EXPECT_EQ(document.removeSoftButton(kOwner, 13).error().code, ErrorCode::NotFound);
}

TEST(ActivityButtons, NewActivityGetsSoftGroupBeforeHardButtons) {
    ConfigDocument document = openSample();
    harmony::ActivityDraft draft;
    draft.label = "Новая";
    draft.devices = {kTv};
    auto id = document.addActivity(draft);
    ASSERT_TRUE(id);

    ASSERT_TRUE(document.setSoftButton(harmony::ButtonOwner::activity(*id), 0, "Вход", kTv, "Home"));

    const std::string xml = document.toXml();
    const auto activity = xml.find("<Label>Новая</Label>");
    const auto soft = xml.find("<ControlGroup name=\"Misc\">", activity);
    const auto hard = xml.find("<ControlGroup name=\"HardButtons\">", activity);
    ASSERT_NE(soft, std::string::npos);
    EXPECT_LT(soft, hard); // как у пульта: экранные кнопки, затем аппаратные
}

// --- Кнопки устройства --------------------------------------------------------

TEST(DeviceButtons, EditsOwnButtonsOnly) {
    ConfigDocument document = openSample();
    const auto owner = harmony::ButtonOwner::device(kTv);

    ASSERT_TRUE(document.setHardButton(owner, "Record", kTv, "Text"));
    EXPECT_EQ(document.setHardButton(owner, "Stop", kReceiver, "Setup").error().code, ErrorCode::Format);

    const auto* tv = document.model().findDevice(kTv);
    bool found = false;
    for (const auto& group : tv->controlGroups) {
        for (const auto& button : group.buttons) {
            found = found || (button.name == "Record" && button.actionId == std::string(kTv) + "_Text_Hold");
        }
    }
    EXPECT_TRUE(found);

    const int position = document.freeSoftPosition(owner);
    EXPECT_EQ(position, 0); // в фикстуре заняты 1 и 2
    ASSERT_TRUE(document.setSoftButton(owner, position, "Сон", kTv, "Text"));
    ASSERT_TRUE(document.moveSoftButton(owner, position, 1)); // обмен с «Питанием»
    ASSERT_TRUE(document.removeSoftButton(owner, 1));
}
