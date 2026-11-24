/** @file remoteModelChangeTest.cpp
 * @brief A6: сводка смены модели до подтверждения, без изменения назначений.
 */
#include "../app/src/widgets/remoteProfileDialog.h"
#include "../app/src/remoteResources.h"
#include "harmony/core/model/remoteConfig.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
namespace app = harmony::app;

/// Синтетические назначения: два вне модели 650, одно допустимое, одна экранная кнопка.
harmony::model::RemoteConfig assignments() {
    harmony::model::RemoteConfig config;
    harmony::model::Device device;
    device.id = "device";
    harmony::model::ControlGroup group;
    group.name = "HardButtons";
    harmony::model::Button outside;
    outside.name = "SoftRight4";
    outside.actionId = "device_Home_Hold";
    group.buttons.push_back(outside);
    auto allowed = outside;
    allowed.name = "Play";
    group.buttons.push_back(allowed);
    auto screen = outside;
    screen.name.clear();
    group.buttons.push_back(screen);
    auto empty = outside;
    empty.name = "MissingButUnassigned";
    empty.actionId.clear();
    group.buttons.push_back(empty);
    device.controlGroups.push_back(group);
    config.devices.push_back(device);
    harmony::model::Activity activity;
    activity.id = "activity";
    group.buttons = {outside};
    activity.controlGroups.push_back(group);
    config.activities.push_back(activity);
    return config;
}

class RemoteModelChange : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(app::remoteResources::reload(root.path()).isEmpty());
    }
    void TearDown() override { app::remoteResources::reload(root.path()); }
};

TEST_F(RemoteModelChange, CountsDeviceAndActivityAssignmentsBeforeConfirmingChange) {
    const auto config = assignments();
    app::RemoteProfileDialog dialog("885", nullptr, &config);
    auto* models = dialog.findChild<QComboBox*>();
    auto* summary = dialog.findChild<QLabel*>("modelChangeSummary");
    ASSERT_NE(models, nullptr);
    ASSERT_NE(summary, nullptr);
    EXPECT_TRUE(summary->isHidden());
    models->setCurrentIndex(models->findData("650"));
    EXPECT_FALSE(summary->isHidden());
    EXPECT_TRUE(summary->text().contains("2"));
    EXPECT_TRUE(summary->text().contains("kept"));
    models->setCurrentIndex(models->findData("880"));
    EXPECT_TRUE(summary->text().contains("not complete"));
    models->setCurrentIndex(models->findData("885"));
    EXPECT_TRUE(summary->isHidden());
    models->setCurrentIndex(models->findData("650"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(buttons, nullptr);
    buttons->button(QDialogButtonBox::Ok)->click();
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
    EXPECT_EQ(dialog.selectedRemote(), "650");
    EXPECT_EQ(config.devices.front().controlGroups.front().buttons.front().name, "SoftRight4");
    EXPECT_EQ(config.activities.front().controlGroups.front().buttons.front().actionId, "device_Home_Hold");
}

TEST_F(RemoteModelChange, CancellationKeepsAssignmentsAndSelectionIsOnlyAProposal) {
    const auto config = assignments();
    app::RemoteProfileDialog dialog("885", nullptr, &config);
    auto* models = dialog.findChild<QComboBox*>();
    ASSERT_NE(models, nullptr);
    models->setCurrentIndex(models->findData("650"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(buttons, nullptr);
    buttons->button(QDialogButtonBox::Cancel)->click();
    EXPECT_EQ(dialog.result(), QDialog::Rejected);
    EXPECT_EQ(config.devices.front().controlGroups.front().buttons.front().name, "SoftRight4");
}

// C4: сводка называет клавиши, которые получат предупреждение C1 после смены модели
// (красное: прежде сводка говорит только число назначений).
TEST_F(RemoteModelChange, SummaryNamesKeysThatWillWarnForTheNewModel) {
    const auto config = assignments();
    app::RemoteProfileDialog dialog("885", nullptr, &config);
    auto* models = dialog.findChild<QComboBox*>();
    auto* summary = dialog.findChild<QLabel*>("modelChangeSummary");
    ASSERT_NE(models, nullptr);
    ASSERT_NE(summary, nullptr);

    models->setCurrentIndex(models->findData("650"));
    EXPECT_TRUE(summary->text().contains("SoftRight4"));
    EXPECT_TRUE(summary->text().contains("2")) << "число назначений остаётся в тексте";

    // Полная модель, которая знает клавишу, — имён в сводке нет.
    models->setCurrentIndex(models->findData("885"));
    EXPECT_TRUE(summary->isHidden());
}

// Неполный список ничего не утверждает: имён клавиш в сводке нет (факт).
TEST_F(RemoteModelChange, IncompleteModelSummaryNamesNoKeys) {
    const auto config = assignments();
    app::RemoteProfileDialog dialog("885", nullptr, &config);
    auto* models = dialog.findChild<QComboBox*>();
    auto* summary = dialog.findChild<QLabel*>("modelChangeSummary");
    ASSERT_NE(models, nullptr);
    ASSERT_NE(summary, nullptr);

    models->setCurrentIndex(models->findData("880"));
    EXPECT_TRUE(summary->text().contains("not complete"));
    EXPECT_FALSE(summary->text().contains("SoftRight4"));
}

TEST_F(RemoteModelChange, ChoiceWithoutDocumentShowsNoChangeSummary) {
    app::RemoteProfileDialog dialog("885");
    auto* models = dialog.findChild<QComboBox*>();
    auto* summary = dialog.findChild<QLabel*>("modelChangeSummary");
    ASSERT_NE(models, nullptr);
    ASSERT_NE(summary, nullptr);
    models->setCurrentIndex(models->findData("650"));
    EXPECT_TRUE(summary->isHidden());
}
} // namespace
