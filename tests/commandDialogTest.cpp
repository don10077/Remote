#include "../app/src/widgets/commandDialog.h"

#include <QApplication>
#include "../app/src/theme.h"
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <gtest/gtest.h>

TEST(CommandDialog, FitsContentWithoutSignalTabs) {
    harmony::ConfigDocument::CommandDetails details;
    harmony::app::CommandDialog dialog("Dune BD PRIME", "PowerToggle", details);
    dialog.show();
    QApplication::processEvents();
    EXPECT_LT(dialog.height(), 400);
    EXPECT_GE(dialog.width(), 440);
    EXPECT_GE(dialog.minimumWidth(), 440);
    EXPECT_TRUE(dialog.findChildren<QTabWidget*>().isEmpty());
    for (const auto* label : dialog.findChildren<QLabel*>()) {
        EXPECT_TRUE(dialog.rect().contains(QRect(label->mapTo(&dialog, QPoint()), label->size())));
    }
    const auto buttons = dialog.findChildren<QPushButton*>();
    ASSERT_EQ(buttons.size(), 1);
    buttons.front()->click();
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (!qEnvironmentVariableIsEmpty("HARMONY_ZONE_PREVIEW_DIR")) harmony::app::theme::apply();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
