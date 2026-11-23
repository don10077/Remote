/**
 * @file hardButtonsWidgetsTest.cpp
 * @brief A3: неназначенные клавиши и перенос команды на строку таблицы без фото.
 */

#include "configFixtures.h"
#include "../app/src/modelKeysQt.h"
#include "../app/src/assignmentGate.h"
#include "../app/src/remoteResources.h"
#include "../app/src/modern/remoteView.h"
#include "../app/src/modern/remotePanel.h"
#include "../app/src/widgets/buttonDialog.h"
#include "../app/src/widgets/configView.h"

#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLayout>
#include <QInputDialog>
#include <QTimer>
#include <QMimeData>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
namespace app = harmony::app;
namespace resources = app::remoteResources;

/// Изолированный снимок ресурсов; документ синтетический и не требует dumps/.
class HardButtonsWidgets : public testing::Test {
protected:
    QTemporaryDir root;
    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }
    void TearDown() override { resources::reload(root.path()); }

    /// Ищет строку клавиши в открытой таблице кнопок.
    static QTableWidget* findKey(app::ConfigView& view, const QString& key, int& row) {
        for (auto* table : view.findChildren<QTableWidget*>()) {
            if (!table->isVisible()) continue;
            for (int index = 0; index < table->rowCount(); ++index) {
                if (table->item(index, 1) && table->item(index, 1)->text() == key) {
                    row = index;
                    return table;
                }
            }
        }
        return nullptr;
    }
};

TEST_F(HardButtonsWidgets, UnassignedFamilyKeyIsVisibleAndReceivesDroppedCommand) {
    auto document = harmony::test::configFixture();
    ASSERT_TRUE(document.setTargetRemote("885"));
    app::ConfigView view;
    view.setConfig(&document);
    view.select(app::ConfigView::Item::Activity, harmony::test::kActivityId);
    view.selectButton(app::ButtonRef::screen(1));
    view.resize(1100, 800);
    view.show();
    QApplication::processEvents();
    int row = -1;
    auto* table = findKey(view, "SoftRight4", row);
    ASSERT_NE(table, nullptr) << "Клавиша семейства должна быть видна до назначения";
    EXPECT_EQ(table->item(row, 0)->text(), "Screen keys");
    EXPECT_EQ(table->item(row, 2)->text(), "— unassigned —");
    view.selectButton(app::ButtonRef::hard("SoftRight4"));
    for (auto* button : view.findChildren<QPushButton*>()) {
        if (button->isVisible() && button->text() == "Clear assignment") {
            EXPECT_FALSE(button->isEnabled());
        }
    }
    int requests = 0;
    QObject::connect(&view, &app::ConfigView::commandAssignRequested, &view,
                     [&](const harmony::ButtonOwner& owner, const app::ButtonRef& button,
                         const QString& deviceId, const QString& command) {
        ++requests;
        EXPECT_EQ(owner.id, harmony::test::kActivityId);
        EXPECT_FALSE(button.soft);
        EXPECT_EQ(button.key, "SoftRight4");
        ASSERT_TRUE(document.setHardButton(owner, button.key.toStdString(),
                                           deviceId.toStdString(), command.toStdString()));
    });
    table->scrollToItem(table->item(row, 1));
    QApplication::processEvents();
    const QPoint point = table->visualItemRect(table->item(row, 1)).center();
    QMimeData mime;
    mime.setData(app::RemoteView::MIME_COMMAND, "invalid payload");
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    QDragEnterEvent invalid(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#else
    QDragEnterEvent invalid(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#endif
    QApplication::sendEvent(table->viewport(), &invalid);
    EXPECT_FALSE(invalid.isAccepted());
    EXPECT_EQ(requests, 0);
    mime.setData(app::RemoteView::MIME_COMMAND,
                 app::RemoteView::commandPayload(harmony::test::kTvId, "Home"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    QDragEnterEvent enter(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#else
    QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#endif
    QApplication::sendEvent(table->viewport(), &enter);
    ASSERT_TRUE(enter.isAccepted());
    QDropEvent drop(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(table->viewport(), &drop);
    EXPECT_TRUE(drop.isAccepted());
    EXPECT_EQ(requests, 0) << "Изменение документа откладывается до завершения drop";
    QApplication::processEvents();
    ASSERT_EQ(requests, 1);
    view.refresh();
    view.selectButton(app::ButtonRef::hard("SoftRight4"));
    QApplication::processEvents();
    table = findKey(view, "SoftRight4", row);
    ASSERT_NE(table, nullptr);
    EXPECT_TRUE(table->item(row, 2)->text().contains("Home"));
}

TEST_F(HardButtonsWidgets, DeviceTableIncludesUnassignedKeysAndPreservesExistingAssignments) {
    auto document = harmony::test::configFixture();
    ASSERT_TRUE(document.setTargetRemote("885"));
    const auto owner = harmony::ButtonOwner::device(harmony::test::kTvId);
    ASSERT_TRUE(document.setHardButton(owner, "LegacyCustom", harmony::test::kTvId, "Home"));
    app::ConfigView view;
    view.setConfig(&document);
    view.select(app::ConfigView::Item::Device, owner.id);
    view.selectButton(app::ButtonRef::screen(1));
    view.show();
    QApplication::processEvents();
    int row = -1;
    auto* table = findKey(view, "SoftRight4", row);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->item(row, 2)->text(), "— unassigned —");
    table = findKey(view, "LegacyCustom", row);
    ASSERT_NE(table, nullptr);
    EXPECT_TRUE(table->item(row, 2)->text().contains("Home"));
}

TEST_F(HardButtonsWidgets, DialogGroupsModelKeysWithNonSelectableCategoryHeadings) {
    auto document = harmony::test::configFixture();
    ASSERT_TRUE(document.setTargetRemote("885"));
    app::ButtonDialog dialog(document.model(), "885",
                             harmony::ButtonOwner::activity(harmony::test::kActivityId),
                             app::ButtonRef::hard("SoftRight4"));
    QComboBox* keys = nullptr;
    for (auto* combo : dialog.findChildren<QComboBox*>()) {
        if (combo->findData("SoftRight4") >= 0) keys = combo;
    }
    ASSERT_NE(keys, nullptr);
    EXPECT_EQ(dialog.button().key, "SoftRight4");
    const int heading = keys->findText("Screen keys");
    ASSERT_GE(heading, 0);
    EXPECT_FALSE(keys->model()->flags(keys->model()->index(heading, 0)) & Qt::ItemIsSelectable);
    EXPECT_FALSE(keys->model()->flags(keys->model()->index(heading, 0)) & Qt::ItemIsEnabled);
    EXPECT_GT(keys->findData("SoftRight4"), heading);
}

TEST_F(HardButtonsWidgets, UnknownModelDialogInitiallySelectsAKeyRatherThanAHeading) {
    auto document = harmony::test::configFixture();
    ASSERT_TRUE(document.setTargetRemote(""));
    app::ButtonDialog dialog(document.model(), "",
                             harmony::ButtonOwner::activity(harmony::test::kActivityId),
                             app::ButtonRef::hard({}));
    EXPECT_FALSE(dialog.button().key.isEmpty());
    QComboBox* keys = nullptr;
    for (auto* combo : dialog.findChildren<QComboBox*>()) {
        if (combo->findData("Play") >= 0) keys = combo;
    }
    ASSERT_NE(keys, nullptr);
    int occurrences = 0;
    for (int index = 0; index < keys->count(); ++index) {
        if (keys->itemData(index).toString() == "VolumeUp") ++occurrences;
    }
    EXPECT_EQ(occurrences, 1) << "Клавиша конфигурации не дублируется при добавлении словаря";
}

TEST_F(HardButtonsWidgets, MissingProfileHintAppearsAboveActivityAndDeviceButtonTables) {
    auto document = harmony::test::configFixture();
    app::ConfigView view;
    view.resize(1100, 800);
    view.show();
    for (const auto* target : {"885", "650", "885"}) {
        SCOPED_TRACE(target);
        ASSERT_TRUE(document.setTargetRemote(target));
        view.setConfig(&document);
        for (const bool deviceOwner : {false, true}) {
            view.select(deviceOwner ? app::ConfigView::Item::Device : app::ConfigView::Item::Activity,
                        deviceOwner ? harmony::test::kTvId : harmony::test::kActivityId);
            view.selectButton(app::ButtonRef::screen(1));
            QApplication::processEvents();
            QLabel* hint = nullptr;
            for (auto* label : view.findChildren<QLabel*>("assignmentProfileHint")) {
                if (label->parentWidget()->isVisible()) hint = label;
            }
            ASSERT_NE(hint, nullptr);
            EXPECT_EQ(hint->isVisible(), QString(target) != "650");
            EXPECT_TRUE(hint->text().contains("assigned in the table"));
            EXPECT_TRUE(hint->text().contains("Edit remote zones"));
            auto* layout = hint->parentWidget()->layout();
            ASSERT_NE(layout, nullptr);
            int tableIndex = -1;
            for (int index = 0; index < layout->count(); ++index) {
                if (qobject_cast<QTableWidget*>(layout->itemAt(index)->widget())) tableIndex = index;
            }
            EXPECT_GE(tableIndex, 0);
            EXPECT_LT(layout->indexOf(hint), tableIndex);
        }
    }
}

TEST_F(HardButtonsWidgets, RemotePictureControlsAndClickHintStayHiddenWithoutProfile) {
    harmony::model::RemoteConfig config;
    harmony::app::RemotePanel panel;
    panel.show();
    for (const auto* target : {"885", "650", "", "885"}) {
        SCOPED_TRACE(target);
        panel.setConfig(&config, target);
        QApplication::processEvents();
        const bool visible = QString(target) == "650";
        EXPECT_EQ(panel.hasProfile(), visible);
        auto* hint = panel.findChild<QLabel*>("remoteHint");
        auto* picture = panel.findChild<QWidget*>("remoteScroll");
        auto* controls = panel.findChild<QWidget*>("remoteColumn");
        ASSERT_NE(hint, nullptr);
        ASSERT_NE(picture, nullptr);
        ASSERT_NE(controls, nullptr);
        EXPECT_EQ(hint->isVisible(), visible);
        EXPECT_EQ(picture->isVisible(), visible);
        EXPECT_EQ(controls->isVisible(), visible);
    }
}

TEST_F(HardButtonsWidgets, OutsideAssignmentCannotBeReassignedButCanMoveToModelKey) {
    auto document = harmony::test::configFixture();
    ASSERT_TRUE(document.setTargetRemote("650"));
    const auto owner = harmony::ButtonOwner::activity(harmony::test::kActivityId);
    ASSERT_TRUE(document.setHardButton(owner, "SoftRight4", harmony::test::kTvId, "Home"));
    const auto keys = app::modelKeysFor("650", &document.model());
    ASSERT_TRUE(keys.complete);
    EXPECT_FALSE(app::buttonAllowed(app::ButtonRef::hard("SoftRight4"), keys,
                                    app::ButtonAction::AssignTo, false));
    {
        app::ButtonDialog dialog(document.model(), "650", owner, app::ButtonRef::hard({}));
        for (auto* combo : dialog.findChildren<QComboBox*>()) {
            EXPECT_LT(combo->findData("SoftRight4"), 0);
        }
    }
    app::ConfigView view;
    view.setConfig(&document);
    view.select(app::ConfigView::Item::Activity, owner.id);
    view.selectButton(app::ButtonRef::hard("SoftRight4"));
    view.show();
    QApplication::processEvents();
    int row = -1;
    auto* table = findKey(view, "SoftRight4", row);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->item(row, 0)->text(), "Not on this model");
    EXPECT_FALSE(table->item(row, 0)->icon().isNull());
    QPushButton* move = nullptr;
    QPushButton* remove = nullptr;
    for (auto* button : view.findChildren<QPushButton*>()) {
        if (!button->isVisible()) continue;
        if (button->text() == "Move to…") move = button;
        if (button->text() == "Clear assignment") remove = button;
    }
    ASSERT_NE(move, nullptr);
    ASSERT_TRUE(move->isEnabled());
    ASSERT_NE(remove, nullptr);
    EXPECT_TRUE(remove->isEnabled());
    int moved = 0;
    QObject::connect(&view, &app::ConfigView::buttonMoveRequested, &view,
                     [&](const harmony::ButtonOwner& requestedOwner, const app::ButtonRef& from,
                         const app::ButtonRef& to, bool copy) {
        ++moved;
        ASSERT_TRUE(document.moveHardButton(requestedOwner, from.key.toStdString(), to.key.toStdString(), copy));
    });
    QTimer::singleShot(0, &view, [] {
        auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(input, nullptr);
        auto* combo = input->findChild<QComboBox*>();
        ASSERT_NE(combo, nullptr);
        EXPECT_LT(combo->findText("SoftRight4"), 0);
        combo->setCurrentText("Play");
        EXPECT_EQ(combo->currentText(), "Play");
        input->accept();
    });
    move->click();
    ASSERT_EQ(moved, 1);
    view.refresh();
    view.selectButton(app::ButtonRef::hard("Play"));
    QApplication::processEvents();
    EXPECT_EQ(findKey(view, "SoftRight4", row), nullptr);
    table = findKey(view, "Play", row);
    ASSERT_NE(table, nullptr);
    EXPECT_TRUE(table->item(row, 2)->text().contains("Home"));
    for (int index = 0; index < table->rowCount(); ++index) {
        EXPECT_NE(table->item(index, 0)->text(), "Not on this model");
    }
    app::ButtonDialog dialog(document.model(), "650", owner, app::ButtonRef::hard({}));
    for (auto* combo : dialog.findChildren<QComboBox*>()) {
        EXPECT_LT(combo->findData("SoftRight4"), 0);
    }
}

TEST_F(HardButtonsWidgets, IncompleteKeyListDoesNotMarkExistingUnknownKeyAsUnavailable) {
    auto document = harmony::test::configFixture();
    ASSERT_TRUE(document.setTargetRemote("880"));
    const auto owner = harmony::ButtonOwner::activity(harmony::test::kActivityId);
    ASSERT_TRUE(document.setHardButton(owner, "CustomIncomplete", harmony::test::kTvId, "Home"));
    app::ConfigView view;
    view.setConfig(&document);
    view.select(app::ConfigView::Item::Activity, owner.id);
    view.selectButton(app::ButtonRef::hard("CustomIncomplete"));
    view.show();
    QApplication::processEvents();
    int row = -1;
    auto* table = findKey(view, "CustomIncomplete", row);
    ASSERT_NE(table, nullptr);
    EXPECT_NE(table->item(row, 0)->text(), "Not on this model");
    EXPECT_TRUE(table->item(row, 0)->icon().isNull());
}

} // namespace
