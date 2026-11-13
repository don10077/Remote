/**
 * @file writePreviewDialog.cpp
 * @brief Окно «что изменится на пульте» перед записью.
 */

#include "writePreviewDialog.h"

#include "../labels.h"
#include "../theme.h"

#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace harmony::app {

void fillChangeTree(QTreeWidget* tree, const std::vector<ConfigChange>& changes,
                    const QString& removedPattern, const QString& addedPattern) {
    tree->clear();
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->header()->setStretchLastSection(true);
    tree->setWordWrap(true);

    // Разделы: устройства, затем активности.
    const std::pair<ConfigChange::Area, QString> areas[] = {
        {ConfigChange::Area::Device, QObject::tr("Devices")},
        {ConfigChange::Area::Activity, QObject::tr("Activities")}};
    for (const auto& [area, name] : areas) {
        QTreeWidgetItem* section = nullptr;
        for (const ConfigChange& change : changes) {
            if (change.area != area) {
                continue;
            }
            if (section == nullptr) {
                section = new QTreeWidgetItem(tree, {name});
                QFont bold = section->font(0);
                bold.setBold(true);
                section->setFont(0, bold);
                section->setFlags(Qt::ItemIsEnabled);
            }
            const QString item = QString::fromStdString(change.item);
            QString text;
            QColor colour;
            switch (change.kind) {
            case ConfigChange::Kind::Added:
                text = addedPattern.isEmpty() ? QObject::tr("+ %1 (new)").arg(item) : addedPattern.arg(item);
                colour = theme::color(theme::Color::Success);
                break;
            case ConfigChange::Kind::Removed:
                text = removedPattern.arg(item);
                colour = theme::color(theme::Color::Danger);
                break;
            case ConfigChange::Kind::Changed:
                text = QObject::tr("● %1").arg(item);
                break;
            }
            auto* row = new QTreeWidgetItem(section, {text});
            if (colour.isValid()) {
                row->setForeground(0, colour);
            }
            for (const Message& detail : change.details) {
                new QTreeWidgetItem(row, {labels::errorText(detail)});
            }
            row->setExpanded(change.details.size() <= 6);
        }
        if (section != nullptr) {
            section->setExpanded(true);
        }
    }
}

WritePreviewDialog::WritePreviewDialog(const std::vector<ConfigChange>& changes,
                                       const QString& backupFile, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Write to remote"));
    setMinimumSize(560, changes.empty() ? 240 : 520);

    auto* title = new QLabel(changes.empty() ? tr("The remote already has this configuration")
                                             : tr("This changes on the remote"),
                             this);
    title->setObjectName("pageTitle");
    title->setWordWrap(true);

    auto* tree = new QTreeWidget(this);
    fillChangeTree(tree, changes, tr("− %1 (removed from the remote)"));
    tree->setVisible(!changes.empty());

    auto* note = new QLabel(tr("The remote's current configuration is saved: %1")
                                .arg(QDir::toNativeSeparators(backupFile)),
                            this);
    note->setObjectName("pageMeta");
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* write =
        new QPushButton(changes.empty() ? tr("Write anyway") : tr("Write to remote"), this);
    write->setObjectName("primaryButton");
    write->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(write);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(tree, 1);
    if (changes.empty()) {
        layout->addStretch(1);
    }
    layout->addWidget(note);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(write, &QPushButton::clicked, this, &QDialog::accept);
}

} // namespace harmony::app
