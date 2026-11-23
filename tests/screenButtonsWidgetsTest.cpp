/**
 * @file screenButtonsWidgetsTest.cpp
 * @brief A2: добавление экранной кнопки через настоящие виджеты, без зависимости от фото.
 */

#include "configFixtures.h"
#include "../app/src/remoteResources.h"
#include "../app/src/widgets/buttonDialog.h"
#include "../app/src/widgets/configView.h"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

namespace app = harmony::app;
namespace resources = app::remoteResources;

/// Один и тот же путь для модели без фото, документа без модели и модели с профилем.
class ScreenButtonsWidgets : public testing::TestWithParam<const char*> {
protected:
    QTemporaryDir root;

    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }

    void TearDown() override { resources::reload(root.path()); }

    /// Проверяет запрос новой кнопки, выбор команды, свободное место и строку таблицы.
    void checkAddition(bool deviceOwner) {
        auto document = harmony::test::configFixture();
        ASSERT_TRUE(document.setTargetRemote(GetParam()));
        const auto owner = deviceOwner ? harmony::ButtonOwner::device(harmony::test::kTvId)
                                       : harmony::ButtonOwner::activity(harmony::test::kActivityId);
        // Занимаем больше одной страницы любого встроенного профиля. Места 0 и 2 свободны.
        for (int position = 1; position <= 12; ++position) {
            if (position == 2) continue;
            ASSERT_TRUE(document.setSoftButton(owner, position, "Occupied", harmony::test::kTvId, "Home"));
        }
        // У фикстуры могут быть исходные кнопки: освобождаем проверяемые места явно.
        ASSERT_TRUE(document.setSoftButton(owner, 0, "Temporary", harmony::test::kTvId, "Home"));
        ASSERT_TRUE(document.setSoftButton(owner, 2, "Temporary", harmony::test::kTvId, "Home"));
        ASSERT_TRUE(document.removeSoftButton(owner, 0));
        ASSERT_TRUE(document.removeSoftButton(owner, 2));
        ASSERT_EQ(document.freeSoftPosition(owner), 0);
        ASSERT_TRUE(document.setSoftButton(owner, 0, "First", harmony::test::kTvId, "Home"));
        ASSERT_EQ(document.freeSoftPosition(owner), 2);

        app::ConfigView view;
        view.setConfig(&document);
        view.select(deviceOwner ? app::ConfigView::Item::Device : app::ConfigView::Item::Activity, owner.id);
        view.selectButton(app::ButtonRef::screen(0));
        view.show();
        QApplication::processEvents();

        QPushButton* add = nullptr;
        for (auto* button : view.findChildren<QPushButton*>()) {
            if (button->text() == "Screen button…" && button->isVisible()) add = button;
        }
        ASSERT_NE(add, nullptr);
        ASSERT_TRUE(add->isEnabled());
        int requests = 0;
        QObject::connect(&view, &app::ConfigView::buttonEditRequested, &view,
                         [&](const harmony::ButtonOwner& requestedOwner, const app::ButtonRef& button) {
            ++requests;
            EXPECT_EQ(requestedOwner.id, owner.id);
            EXPECT_EQ(requestedOwner.isDevice(), deviceOwner);
            EXPECT_TRUE(button.soft);
            EXPECT_LT(button.position, 0);
        });
        add->click();
        ASSERT_EQ(requests, 1);

        app::ButtonDialog dialog(document.model(), document.targetRemote(), owner,
                                 app::ButtonRef::screen(-1));
        auto* commands = dialog.findChild<QListWidget*>();
        ASSERT_NE(commands, nullptr);
        const auto matches = commands->findItems("Home", Qt::MatchExactly);
        ASSERT_EQ(matches.size(), 1);
        commands->setCurrentItem(matches.front());
        for (auto* edit : dialog.findChildren<QLineEdit*>()) {
            if (edit->placeholderText() == "empty — command name") edit->setText("Added screen command");
        }
        ASSERT_EQ(dialog.command(), "Home");
        ASSERT_EQ(dialog.label(), "Added screen command");
        QPushButton* done = nullptr;
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Done") done = button;
        }
        ASSERT_NE(done, nullptr);
        ASSERT_TRUE(done->isEnabled());
        done->click();
        ASSERT_EQ(dialog.result(), QDialog::Accepted);
        auto target = dialog.button();
        ASSERT_TRUE(target.soft);
        ASSERT_LT(target.position, 0);
        // Как в MainWindow::editButton: новую позицию выбирает документ, а не профиль.
        target.position = document.freeSoftPosition(owner);
        ASSERT_EQ(target.position, 2);
        ASSERT_TRUE(document.setSoftButton(owner, target.position, dialog.label(),
                                           dialog.deviceId(), dialog.command()));
        EXPECT_EQ(document.freeSoftPosition(owner), 13);
        view.refresh();
        view.selectButton(target);
        QApplication::processEvents();

        bool found = false;
        for (const auto* table : view.findChildren<QTableWidget*>()) {
            if (!table->isVisible()) continue;
            for (int row = 0; row < table->rowCount(); ++row) {
                if (table->item(row, 1) != nullptr &&
                    table->item(row, 1)->text() == "Added screen command") {
                    found = true;
                    EXPECT_EQ(table->item(row, 0)->text(), "Screen");
                    EXPECT_TRUE(table->item(row, 2)->text().contains("Home"));
                }
            }
        }
        EXPECT_TRUE(found) << "Новая экранная кнопка должна быть видна в таблице";
    }
};

TEST_P(ScreenButtonsWidgets, ActivityAddsScreenButtonToFirstFreePosition) { checkAddition(false); }
TEST_P(ScreenButtonsWidgets, DeviceAddsScreenButtonToFirstFreePosition) { checkAddition(true); }

INSTANTIATE_TEST_SUITE_P(TargetModels, ScreenButtonsWidgets, testing::Values("885", "", "650"));

} // namespace
