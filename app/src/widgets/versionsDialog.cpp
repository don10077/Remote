/**
 * @file versionsDialog.cpp
 * @brief Окно версий конфигурации.
 */

#include "versionsDialog.h"

#include "../labels.h"
#include "writePreviewDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

constexpr int kIndexRole = Qt::UserRole + 1;

} // namespace

VersionsDialog::VersionsDialog(const ConfigDocument& document, QWidget* parent)
    : QDialog(parent), document_(document), key_(versions::keyOf(document)) {
    setWindowTitle(tr("Versions"));
    setMinimumSize(820, 520);

    auto* title =
        new QLabel(tr("Versions of “%1”").arg(QString::fromStdString(document.name())), this);
    title->setObjectName("pageTitle");
    auto* hint = new QLabel(
        tr("The program keeps the configuration's earlier states: automatically every few "
           "minutes while there are unsaved changes, before every save and when you ask. "
           "On the right — what changes if the selected version is restored."),
        this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(2);
    list_->setHeaderLabels({tr("Saved"), tr("Why")});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    changes_ = new QTreeWidget(this);
    status_ = new QLabel(this);
    status_->setObjectName("pageMeta");
    status_->setWordWrap(true);
    status_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(status_);
    rightLayout->addWidget(changes_, 1);
    rightLayout->addStretch(0); // без отличий надпись — сверху, а не посередине

    auto* splitter = new QSplitter(this);
    splitter->addWidget(list_);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    auto* saveNow = new QPushButton(tr("Save version now"), this);
    auto* openFolder = new QPushButton(tr("Folder"), this);
    openFolder->setToolTip(QDir::toNativeSeparators(versions::folder(key_)));
    restore_ = new QPushButton(tr("Restore"), this);
    restore_->setObjectName("primaryButton");
    auto* close = new QPushButton(tr("Close"), this);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(saveNow);
    buttons->addWidget(openFolder);
    buttons->addStretch(1);
    buttons->addWidget(close);
    buttons->addWidget(restore_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);

    connect(list_, &QTreeWidget::currentItemChanged, this, &VersionsDialog::showSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(openFolder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(versions::folder(key_))); });
    connect(saveNow, &QPushButton::clicked, this, [this] {
        if (auto saved = versions::save(document_, key_, versions::Kind::Manual); !saved) {
            QMessageBox::warning(this, tr("Versions"), labels::errorText(saved.error()));
            return;
        }
        fill();
    });
    connect(restore_, &QPushButton::clicked, this, [this] {
        const QTreeWidgetItem* item = list_->currentItem();
        if (item == nullptr) {
            return;
        }
        chosen_ = versions_.value(item->data(0, kIndexRole).toInt());
        accept();
    });
    fill();
    resize(940, 600);
}

void VersionsDialog::fill() {
    versions_ = versions::list(key_);
    list_->clear();
    const QLocale locale;
    for (int i = 0; i < versions_.size(); ++i) {
        const versions::Version& version = versions_[i];
        // С секундами: версии одной минуты иначе не различить.
        const QString when = locale.toString(version.time.date(), QLocale::ShortFormat) +
                             QLatin1Char(' ') +
                             version.time.time().toString(QStringLiteral("HH:mm:ss"));
        auto* item = new QTreeWidgetItem(list_, {when, versions::kindName(version.kind)});
        item->setToolTip(0, QDir::toNativeSeparators(version.path));
        item->setData(0, kIndexRole, i);
    }
    if (list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    } else {
        changes_->clear();
        changes_->hide();
        status_->setText(
            tr("No versions yet. They appear while you edit (every few minutes), before saving and "
               "with “Save version now”."));
    }
    restore_->setEnabled(!versions_.isEmpty());
}

void VersionsDialog::showSelected() {
    const QTreeWidgetItem* item = list_->currentItem();
    restore_->setEnabled(item != nullptr);
    if (item == nullptr) {
        return;
    }
    const versions::Version version = versions_.value(item->data(0, kIndexRole).toInt());
    auto old = versions::open(version, document_.name());
    if (!old) {
        changes_->hide();
        status_->setText(labels::errorText(old.error()));
        restore_->setEnabled(false);
        return;
    }
    // Что изменится в открытой конфигурации, если восстановить версию.
    const auto changes = compareConfigurations(document_, *old);
    if (changes.empty()) {
        changes_->hide();
        status_->setText(tr("Same as the open configuration."));
        return;
    }
    status_->setText(tr("If restored:"));
    fillChangeTree(changes_, changes, tr("− %1 (removed)"));
    changes_->show();
}

} // namespace harmony::app
