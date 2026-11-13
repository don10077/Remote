/**
 * @file checkDialog.cpp
 * @brief Окно проверки конфигурации.
 */

#include "checkDialog.h"

#include "../labels.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace harmony::app {

namespace {

constexpr int kIndexRole = Qt::UserRole + 1;

/// Перекрашивает кнопку как главную или обычную.
void setPrimary(QPushButton* button, bool primary) {
    button->setObjectName(primary ? "primaryButton" : "");
    button->style()->unpolish(button);
    button->style()->polish(button);
}

} // namespace

CheckDialog::CheckDialog(const ConfigDocument& document, Apply apply, QWidget* parent)
    : QDialog(parent), document_(document), apply_(std::move(apply)) {
    setWindowTitle(tr("Check configuration"));
    setMinimumSize(760, 480);
    resize(900, 560);

    auto* title = new QLabel(tr("Check configuration"), this);
    title->setObjectName("pageTitle");
    summary_ = new QLabel(this);
    summary_->setObjectName("pageMeta");
    summary_->setWordWrap(true);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(3);
    list_->setHeaderLabels({tr("Where"), tr("Problem"), tr("Auto")});
    list_->headerItem()->setToolTip(2, tr("✓ — fixes itself (“Fix”, “Fix all”)"));
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setWordWrap(true);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    list_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    list_->setTextElideMode(Qt::ElideMiddle);

    show_ = new QPushButton(tr("Show"), this);
    show_->setToolTip(tr("Close this window and open the place of the problem"));
    fix_ = new QPushButton(tr("Fix"), this);
    fixAll_ = new QPushButton(this);
    auto* close = new QPushButton(tr("Close"), this);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(show_);
    buttons->addWidget(fix_);
    buttons->addWidget(fixAll_);
    buttons->addStretch(1);
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(summary_);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);

    connect(list_, &QTreeWidget::currentItemChanged, this, &CheckDialog::updateButtons);
    connect(list_, &QTreeWidget::itemDoubleClicked, show_, &QPushButton::click);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(show_, &QPushButton::clicked, this, [this] {
        if (const Problem* problem = selected()) {
            target_ = *problem;
            accept();
        }
    });
    connect(fix_, &QPushButton::clicked, this, [this] {
        const Problem* problem = selected();
        if (problem == nullptr || !problem->fixable()) {
            return;
        }
        const Problem copy = *problem; // список заполнится заново
        const int row = list_->indexOfTopLevelItem(list_->currentItem());
        apply_(tr("Fix: %1").arg(labels::errorText(copy.what)),
               [&copy](ConfigDocument& target) { return target.fixProblem(copy); });
        refresh();
        list_->setCurrentItem(list_->topLevelItem(std::min(row, list_->topLevelItemCount() - 1)));
    });
    connect(fixAll_, &QPushButton::clicked, this, [this] {
        std::size_t count = 0;
        apply_(tr("Fix all problems"), [&count](ConfigDocument& target) -> Result<void> {
            auto fixed = target.fixAll();
            if (!fixed) {
                return std::unexpected(std::move(fixed.error()));
            }
            count = *fixed;
            return {};
        });
        refresh();
        summary_->setText(tr("Fixed: %1. %2").arg(count).arg(summary_->text()));
    });
    refresh();
}

void CheckDialog::refresh() {
    problems_ = document_.check();
    list_->clear();
    const QIcon errorIcon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
    const QIcon warningIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    for (std::size_t i = 0; i < problems_.size(); ++i) {
        const Problem& problem = problems_[i];
        auto* item = new QTreeWidgetItem(list_);
        item->setIcon(0, problem.severity == Problem::Severity::Error ? errorIcon : warningIcon);
        item->setText(0, labels::errorText(problem.place));
        item->setText(1, labels::errorText(problem.what));
        item->setToolTip(0, item->text(0));
        item->setToolTip(1, item->text(1));
        if (problem.fixable()) {
            item->setText(2, QStringLiteral("✓"));
            item->setTextAlignment(2, Qt::AlignCenter);
            item->setToolTip(2, tr("fixes itself"));
        }
        item->setData(0, kIndexRole, static_cast<qulonglong>(i));
    }
    const auto errors = static_cast<qsizetype>(countProblems(problems_, Problem::Severity::Error));
    const auto warnings =
        static_cast<qsizetype>(countProblems(problems_, Problem::Severity::Warning));
    if (problems_.empty()) {
        summary_->setText(
            tr("No problems found: everything will work on the remote as configured."));
    } else if (errors > 0) {
        summary_->setText(
            tr("%1 and %2. Errors: buttons or steps that will not work on the remote. "
               "Warnings: it works, but probably not as intended.")
                .arg(labels::plural(errors, "error", "errors"),
                     labels::plural(warnings, "warning", "warnings")));
    } else {
        summary_->setText(tr("%1: it works, but probably not as intended.")
                              .arg(labels::plural(warnings, "warning", "warnings")));
    }
    const auto fixable =
        std::ranges::count_if(problems_, [](const Problem& p) { return p.fixable(); });
    fixAll_->setText(fixable > 0 ? tr("Fix all (%1)").arg(fixable) : tr("Fix all"));
    fixAll_->setEnabled(fixable > 0);
    setPrimary(fixAll_, fixable > 0);
    // «Где» — по содержимому, но не шире 320 px: длинное сокращается посередине (подсказка —
    // целиком).
    list_->resizeColumnToContents(0);
    list_->setColumnWidth(0, std::min(list_->columnWidth(0) + 12, 320));
    if (list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
    updateButtons();
}

void CheckDialog::updateButtons() {
    const Problem* problem = selected();
    show_->setEnabled(problem != nullptr && problem->where != Problem::Where::Config);
    fix_->setEnabled(problem != nullptr && problem->fixable());
}

const Problem* CheckDialog::selected() const {
    const QTreeWidgetItem* item = list_->currentItem();
    if (item == nullptr) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(item->data(0, kIndexRole).toULongLong());
    return index < problems_.size() ? &problems_[index] : nullptr;
}

} // namespace harmony::app
