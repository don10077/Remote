/**
 * @file newDeviceDialog.cpp
 * @brief Окно нового устройства.
 */

#include "newDeviceDialog.h"

#include "../icons.h"
#include "../labels.h"
#include "../nameCheck.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace harmony::app {

NewDeviceDialog::NewDeviceDialog(const model::RemoteConfig& config, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New device"));
    setMinimumWidth(460);

    auto* title = new QLabel(tr("New device"), this);
    title->setObjectName("pageTitle");
    auto* hint = new QLabel(tr("A device without commands — learn them from the original remote. If "
                               "you learn PowerOn and PowerOff (or PowerToggle), the remote will turn "
                               "the device on by itself."),
                            this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    label_ = new QLineEdit(this);
    label_->setPlaceholderText(tr("For example, Bedroom projector"));
    type_ = new QComboBox(this);
    for (const auto& type : labels::deviceTypes()) {
        type_->addItem(icons::deviceType(type), labels::deviceType(type), QString::fromStdString(type));
    }
    manufacturer_ = new QLineEdit(this);
    model_ = new QLineEdit(this);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Name:"), label_);
    nameProblem_ = new QLabel(tr("Another device already has this name: the remote shows only names."), this);
    nameProblem_->setObjectName("fieldProblem");
    nameProblem_->setWordWrap(true);
    nameProblem_->setVisible(false);
    form->addRow(QString(), nameProblem_);
    form->addRow(tr("Type:"), type_);
    form->addRow(tr("Manufacturer:"), manufacturer_);
    form->addRow(tr("Model:"), model_);

    auto* cancel = new QPushButton(tr("Cancel"), this);
    create_ = new QPushButton(tr("Create"), this);
    create_->setObjectName("primaryButton");
    create_->setDefault(true);
    create_->setEnabled(false);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(create_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addSpacing(4);
    layout->addLayout(form);
    layout->addLayout(buttons);

    // Окно модальное: конфигурация, пока оно открыто, не меняется.
    connect(label_, &QLineEdit::textChanged, this, [this, known = &config](const QString& text) {
        const bool taken = deviceNameTaken(*known, text);
        nameProblem_->setVisible(taken);
        create_->setEnabled(!text.trimmed().isEmpty() && !taken);
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(create_, &QPushButton::clicked, this, &QDialog::accept);
}

DeviceDraft NewDeviceDialog::draft() const {
    return {.label = label_->text().trimmed().toStdString(),
            .type = type_->currentData().toString().toStdString(),
            .manufacturer = manufacturer_->text().trimmed().toStdString(),
            .model = model_->text().trimmed().toStdString()};
}

} // namespace harmony::app
