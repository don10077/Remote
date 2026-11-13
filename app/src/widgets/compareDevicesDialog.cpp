/**
 * @file compareDevicesDialog.cpp
 * @brief Окно сравнения команд двух устройств.
 */

#include "compareDevicesDialog.h"

#include "../icons.h"
#include "../labels.h"
#include "../theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kNameRole = Qt::UserRole + 1;
using Status = CommandComparison::Status;

/// Цвет итога сравнения (из палитры — подходит и к тёмной теме).
QColor tint(Status status) {
    switch (status) {
    case Status::Same: return theme::color(theme::Color::Success);
    case Status::Different: return theme::color(theme::Color::Warning);
    case Status::OnlyFirst: return theme::color(theme::Color::Accent);
    case Status::OnlySecond: return theme::color(theme::Color::TextMuted);
    }
    return {};
}

} // namespace

CompareDevicesDialog::CompareDevicesDialog(const ConfigDocument& document, const std::string& deviceId,
                                           QWidget* parent)
    : QDialog(parent), document_(document), first_(deviceId) {
    setWindowTitle(tr("Compare devices"));
    setMinimumSize(680, 560);

    title_ = new QLabel(this);
    title_->setObjectName("pageTitle");
    title_->setWordWrap(true);
    auto* hint = new QLabel(tr("Commands with the same name side by side. Marked commands are copied from the left "
                               "device to the right one; a command with the same name is replaced."),
                            this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    second_ = new QComboBox(this);
    auto* swap = new QToolButton(this);
    swap->setText(QStringLiteral("⇄"));
    swap->setToolTip(tr("Swap the devices"));
    onlyDifferences_ = new QCheckBox(tr("Only differences"), this);
    onlyDifferences_->setChecked(true);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Compare with:"), this));
    top->addWidget(second_, 1);
    top->addWidget(swap);
    top->addSpacing(12);
    top->addWidget(onlyDifferences_);

    table_ = new QTableWidget(0, 4, this);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(false);

    summary_ = new QLabel(this);
    summary_->setObjectName("pageMeta");
    summary_->setWordWrap(true);

    markMissing_ = new QPushButton(tr("Mark missing"), this);
    markMissing_->setToolTip(tr("Mark the commands that only the left device has"));
    auto* close = new QPushButton(tr("Close"), this);
    copy_ = new QPushButton(this);
    copy_->setObjectName("primaryButton");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(markMissing_);
    buttons->addStretch(1);
    buttons->addWidget(close);
    buttons->addWidget(copy_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title_);
    layout->addWidget(hint);
    layout->addLayout(top);
    layout->addWidget(table_, 1);
    layout->addWidget(summary_);
    layout->addLayout(buttons);

    // Правое — все остальные; сначала того же типа (телевизор с телевизором).
    const model::RemoteConfig& config = document.model();
    const model::Device* own = config.findDevice(deviceId);
    const std::string type = own != nullptr ? own->type : std::string();
    for (const bool sameType : {true, false}) {
        for (const auto& device : config.devices) {
            if (device.id != deviceId && (device.type == type) == sameType) {
                second_->addItem(icons::deviceType(device.type), QString::fromStdString(device.displayName()),
                                 QString::fromStdString(device.id));
            }
        }
    }

    connect(second_, &QComboBox::currentIndexChanged, this, &CompareDevicesDialog::fill);
    connect(onlyDifferences_, &QCheckBox::toggled, this, &CompareDevicesDialog::fill);
    connect(swap, &QToolButton::clicked, this, [this] {
        const std::string other = toId();
        if (other.empty()) {
            return;
        }
        // Левое уходит в список вместо правого.
        const int index = second_->currentIndex();
        const model::Device* left = document_.model().findDevice(first_);
        const QSignalBlocker blocker(second_);
        second_->setItemText(index, nameOf(first_));
        second_->setItemData(index, QString::fromStdString(first_));
        if (left != nullptr) {
            second_->setItemIcon(index, icons::deviceType(left->type));
        }
        first_ = other;
        fill();
    });
    connect(table_, &QTableWidget::itemChanged, this, &CompareDevicesDialog::updateSummary);
    connect(markMissing_, &QPushButton::clicked, this, [this] {
        for (int row = 0; row < table_->rowCount(); ++row) {
            QTableWidgetItem* item = table_->item(row, 0);
            if ((item->flags() & Qt::ItemIsUserCheckable) != 0 && table_->item(row, 2)->text().isEmpty()) {
                item->setCheckState(Qt::Checked);
            }
        }
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(copy_, &QPushButton::clicked, this, &QDialog::accept);
    fill();
    resize(780, 640);
}

QString CompareDevicesDialog::nameOf(const std::string& id) const {
    const model::Device* device = document_.model().findDevice(id);
    return device != nullptr ? QString::fromStdString(device->displayName()) : QString();
}

std::string CompareDevicesDialog::toId() const {
    return second_->currentData().toString().toStdString();
}

void CompareDevicesDialog::fill() {
    const QSignalBlocker blocker(table_);
    table_->setRowCount(0);
    const std::string second = toId();
    title_->setText(second.empty() ? tr("Compare “%1”").arg(nameOf(first_))
                                   : tr("“%1” and “%2”").arg(nameOf(first_), nameOf(second)));
    table_->setHorizontalHeaderLabels({tr("Command"), nameOf(first_), nameOf(second), tr("Result")});
    for (const auto& command : document_.compareDeviceCommands(first_, second)) {
        if (onlyDifferences_->isChecked() && command.status == Status::Same) {
            continue;
        }
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* name = new QTableWidgetItem(QString::fromStdString(command.name));
        name->setData(kNameRole, QString::fromStdString(command.name));
        // Копировать можно то, что есть слева и отличается справа.
        if (command.status == Status::Different || command.status == Status::OnlyFirst) {
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            name->setCheckState(Qt::Unchecked);
        } else {
            name->setFlags(Qt::ItemIsEnabled);
        }
        QString result;
        switch (command.status) {
        case Status::Same: result = tr("same"); break;
        case Status::Different: result = tr("different"); break;
        case Status::OnlyFirst: result = tr("only on the left"); break;
        case Status::OnlySecond: result = tr("only on the right"); break;
        }
        // Сигнал — как в окне команды: протокол, адрес, команда (по-русски — по-русски).
        auto summary = [&](const std::string& id, bool present) {
            if (!present) {
                return QString();
            }
            const auto signal = document_.commandSignal(id, command.name);
            return signal ? labels::signalSummary(*signal) : tr("signal is not readable");
        };
        auto* first = new QTableWidgetItem(summary(first_, command.status != Status::OnlySecond));
        auto* other = new QTableWidgetItem(summary(second, command.status != Status::OnlyFirst));
        auto* status = new QTableWidgetItem(result);
        status->setForeground(tint(command.status));
        for (QTableWidgetItem* item : {name, first, other, status}) {
            item->setToolTip(item->text());
        }
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, first);
        table_->setItem(row, 2, other);
        table_->setItem(row, 3, status);
    }
    updateSummary();
}

void CompareDevicesDialog::updateSummary() {
    int same = 0;
    int different = 0;
    int onlyFirst = 0;
    int onlySecond = 0;
    for (const auto& command : document_.compareDeviceCommands(first_, toId())) {
        switch (command.status) {
        case Status::Same: ++same; break;
        case Status::Different: ++different; break;
        case Status::OnlyFirst: ++onlyFirst; break;
        case Status::OnlySecond: ++onlySecond; break;
        }
    }
    summary_->setText(toId().empty()
                          ? tr("There is no other device to compare with.")
                          : tr("Same: %1 · different: %2 · only on the left: %3 · only on the right: %4.")
                                .arg(same)
                                .arg(different)
                                .arg(onlyFirst)
                                .arg(onlySecond));
    const auto count = static_cast<int>(marked().size());
    copy_->setText(count > 0 ? tr("Copy %1 to “%2”").arg(labels::plural(count, "command", "commands"), nameOf(toId()))
                             : tr("Copy marked"));
    copy_->setEnabled(count > 0);
    markMissing_->setEnabled(onlyFirst > 0);
}

std::vector<std::string> CompareDevicesDialog::marked() const {
    std::vector<std::string> names;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QTableWidgetItem* item = table_->item(row, 0);
        if ((item->flags() & Qt::ItemIsUserCheckable) != 0 && item->checkState() == Qt::Checked) {
            names.push_back(item->data(kNameRole).toString().toStdString());
        }
    }
    return names;
}

} // namespace harmony::app
