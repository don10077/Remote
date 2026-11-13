/**
 * @file commandDialog.cpp
 * @brief Окно ИК-команды.
 */

#include "commandDialog.h"

#include "../labels.h"

#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

/// Моноширинный шрифт для кодов.
QFont monoFont() {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"),
                      QStringLiteral("monospace")});
    return mono;
}

QString hex(std::uint32_t value) {
    return QStringLiteral("0x") + QString::number(value, 16).toUpper().rightJustified(value > 0xFF ? 4 : 2, '0');
}

} // namespace

CommandDialog::CommandDialog(const QString& device, const QString& command,
                             const ConfigDocument::CommandDetails& details, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Command “%1”").arg(command));
    constexpr int minimumWidth = 440;

    auto* title = new QLabel(command, this);
    title->setObjectName("pageTitle");
    auto* meta = new QLabel(device, this);
    meta->setObjectName("pageMeta");
    meta->setWordWrap(true);
    title->setWordWrap(true);

    // Что за код: протокол, адрес, команда, данные как в IRremote и биты в порядке передачи.
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(12);
    auto value = [this](const QString& text) {
        auto* label = new QLabel(text, this);
        label->setFont(monoFont());
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    if (details.decoded) {
        form->addRow(tr("Protocol:"), new QLabel(labels::protocol(details.decoded->protocol), this));
        form->addRow(tr("Address:"), value(hex(details.decoded->address)));
        form->addRow(tr("Command:"), value(hex(details.decoded->command)));
        form->addRow(tr("Data (IRremote):"), value(QString::fromStdString(details.decoded->rawData())));
        form->addRow(tr("Bits in order:"),
                     value(tr("%1 · %2 bits")
                               .arg(QString::fromStdString(ir::bitsToHex(details.decoded->bits)))
                               .arg(details.decoded->bits.size())));
    } else {
        form->addRow(tr("Protocol:"), new QLabel(tr("not recognised"), this));
        if (!details.codeBits.empty()) {
            form->addRow(tr("Bits in order:"), value(tr("%1 · %2 bits")
                                                          .arg(QString::fromStdString(ir::bitsToHex(details.codeBits)))
                                                          .arg(details.codeBits.size())));
        }
    }

    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    // Для QVBoxLayout strut задаёт минимальную ширину содержимого.
    const auto margins = layout->contentsMargins();
    layout->addStrut(minimumWidth - margins.left() - margins.right());
    layout->setSpacing(10);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addWidget(title);
    layout->addWidget(meta);
    layout->addSpacing(12);
    layout->addLayout(form);
    layout->addSpacing(16);
    layout->addLayout(buttons);
    // Высота не фиксируется: переведённый текст при узком окне переносится на лишние строки.
    adjustSize();
}

} // namespace harmony::app
