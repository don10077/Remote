/**
 * @file compareConfigsDialog.cpp
 * @brief Окно сравнения открытой конфигурации с другой.
 */

#include "compareConfigsDialog.h"

#include "../configLibrary.h"
#include "../documentSource.h"
#include "../labels.h"
#include "writePreviewDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

/// Имя конфигурации по пути: папка или файл без расширения.
QString nameOf(const QString& path) {
    const QFileInfo info(path);
    return info.isDir() ? info.fileName() : info.completeBaseName();
}

} // namespace

CompareConfigsDialog::CompareConfigsDialog(const ConfigDocument& current, QWidget* parent)
    : QDialog(parent), current_(current) {
    setWindowTitle(tr("Compare configurations"));
    setMinimumSize(600, 560);

    auto* title = new QLabel(tr("How “%1” differs from another configuration")
                                 .arg(QString::fromStdString(current.name())),
                             this);
    title->setObjectName("pageTitle");
    title->setWordWrap(true);

    // Недавние, кроме открытой.
    source_ = new QComboBox(this);
    source_->addItem(tr("— choose —"), QString());
    const QString own = library::normalized(fromPath(current.location()));
    for (const QString& path : library::recent()) {
        if (library::normalized(path) != own) {
            source_->addItem(nameOf(path), path);
            source_->setItemData(source_->count() - 1, QDir::toNativeSeparators(path), Qt::ToolTipRole);
        }
    }
    auto* browse = new QPushButton(tr("Browse…"), this);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Compare with:"), this));
    row->addWidget(source_, 1);
    row->addWidget(browse);

    summary_ = new QLabel(tr("Choose a configuration: a recent one or a file (.EZHex, UserConfiguration.xml)."),
                          this);
    summary_->setObjectName("pageMeta");
    summary_->setWordWrap(true);
    tree_ = new QTreeWidget(this);

    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addLayout(row);
    layout->addWidget(summary_);
    layout->addWidget(tree_, 1);
    layout->addLayout(buttons);

    connect(source_, &QComboBox::currentIndexChanged, this, [this] {
        const QString path = source_->currentData().toString();
        if (!path.isEmpty()) {
            compareWith(path);
        }
    });
    connect(browse, &QPushButton::clicked, this, &CompareConfigsDialog::browse);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    resize(680, 640);
}

void CompareConfigsDialog::browse() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Compare with"), library::lastDialogDir(),
        tr("Remote configuration (*.EZHex *.ezhex UserConfiguration.xml);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    library::setLastDialogDir(QFileInfo(path).absolutePath());
    compareWith(path);
}

void CompareConfigsDialog::compareWith(const QString& path) {
    // Выбранное — в списке (если его там не было — добавляется).
    int index = source_->findData(path);
    if (index < 0) {
        source_->addItem(nameOf(path), path);
        index = source_->count() - 1;
    }
    {
        const QSignalBlocker blocker(source_);
        source_->setCurrentIndex(index);
    }
    auto document = ConfigDocument::open(toPath(path));
    if (!document) {
        other_.reset();
        tree_->clear();
        summary_->setText(tr("Could not open: %1").arg(labels::errorText(document.error())));
        return;
    }
    other_ = std::move(*document);
    showDifferences();
}

void CompareConfigsDialog::showDifferences() {
    const QString otherName = QString::fromStdString(other_->name());
    // Было (другая) → стало (открытая): «новое» есть только в открытой.
    const auto changes = compareConfigurations(*other_, current_);
    fillChangeTree(tree_, changes, tr("− %1 (only in “%2”)").arg(QStringLiteral("%1"), otherName),
                   tr("+ %1 (only in this one)"));
    tree_->setVisible(!changes.empty());
    if (changes.empty()) {
        summary_->setText(tr("No differences: devices, commands, activities and buttons are the same."));
        return;
    }
    int devices = 0;
    int activities = 0;
    for (const ConfigChange& change : changes) {
        (change.area == ConfigChange::Area::Device ? devices : activities) += 1;
    }
    summary_->setText(tr("Differ: %1, %2. “−” — only in “%3”, “+” — only in the open one, “●” — what changed "
                         "from “%3” to the open one.")
                          .arg(labels::plural(devices, "device", "devices"),
                               labels::plural(activities, "activity", "activities"), otherName));
}

} // namespace harmony::app
