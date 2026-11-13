#include "../app/src/widgets/protocolCommandDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <gtest/gtest.h>

TEST(ProtocolCommandDialog, ThreeInputModesAndValidation) {
    harmony::app::ProtocolCommandDialog dialog("TV", {}, "Custom", std::nullopt);
    auto* mode = dialog.findChild<QComboBox*>("commandInputMode");
    auto* raw = dialog.findChild<QLineEdit*>("commandRaw");
    auto* bits = dialog.findChild<QLineEdit*>("commandBits");
    auto* save = dialog.findChild<QPushButton*>("primaryButton");
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(dialog.findChild<QComboBox*>("commandName")->currentText(), "Custom");
    EXPECT_FALSE(save->isEnabled());
    mode->setCurrentIndex(1);
    raw->setText("0xF708FB04");
    EXPECT_TRUE(save->isEnabled());
    EXPECT_EQ(bits->text().size(), 32);
    const auto preview = qEnvironmentVariable("HARMONY_ZONE_PREVIEW_DIR");
    if (!preview.isEmpty()) {
        dialog.show();
        QApplication::processEvents();
        EXPECT_TRUE(dialog.grab().save(preview + "/manual-command.png"));
    }
    mode->setCurrentIndex(2);
    EXPECT_TRUE(save->isEnabled());
    bits->setText("01x");
    EXPECT_FALSE(save->isEnabled());
    mode->setCurrentIndex(1);
    raw->setText("0x1F708FB04");
    EXPECT_FALSE(save->isEnabled());
    raw->setText("0xF708FB04");
    quint32 address = 0, command = 0;
    QObject::connect(&dialog, &harmony::app::ProtocolCommandDialog::commandReady,
                     [&](const QString&, const QString&, quint32 a, quint32 c) { address = a; command = c; });
    save->click();
    EXPECT_EQ(address, 4U);
    EXPECT_EQ(command, 8U);
}

TEST(ProtocolCommandDialog, SaveNextFromRawKeepsAddressAndIncrementsCommand) {
    harmony::app::ProtocolCommandDialog dialog("TV", {}, "PowerOn", std::nullopt);
    dialog.findChild<QComboBox*>("commandInputMode")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("commandRaw")->setText("0xF708FB04");
    dialog.findChild<QPushButton*>("commandSaveNext")->click();
    EXPECT_EQ(dialog.findChild<QLineEdit*>("commandAddress")->text(), "0x0004");
    EXPECT_EQ(dialog.findChild<QLineEdit*>("commandCode")->text(), "0x09");
}

TEST(ProtocolCommandDialog, ContentFits) {
    harmony::app::ProtocolCommandDialog dialog("TV", {}, "PowerOn", std::nullopt);
    dialog.show();
    QApplication::processEvents();
    for (const auto* label : dialog.findChildren<QLabel*>()) {
        if (label->isVisible()) { EXPECT_TRUE(dialog.rect().contains(QRect(label->mapTo(&dialog, QPoint()), label->size()))); }
    }
}
