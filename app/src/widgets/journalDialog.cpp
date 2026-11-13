/**
 * @file journalDialog.cpp
 * @brief Окно журнала программы.
 */

#include "journalDialog.h"

#include "../theme.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QScrollBar>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

/// Короткое название важности для строки журнала.
QString levelText(journal::Level level) {
    switch (level) {
    case journal::Level::Error: return QObject::tr("error");
    case journal::Level::Warning: return QObject::tr("warning");
    case journal::Level::Info: return QObject::tr("action");
    case journal::Level::Debug: return QObject::tr("detail");
    }
    return {};
}

} // namespace

JournalDialog::JournalDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Program journal"));
    setMinimumSize(720, 440);

    auto* title = new QLabel(tr("Program journal"), this);
    title->setObjectName("pageTitle");
    auto* hint = new QLabel(tr("What the program did with the remote and the configuration, and what went wrong. "
                               "How much is recorded — Settings → General."),
                            this);
    hint->setObjectName("pageMeta");
    hint->setWordWrap(true);

    filter_ = new QComboBox(this);
    filter_->addItem(tr("All records"), static_cast<int>(journal::Level::Debug));
    filter_->addItem(tr("Actions, warnings and errors"), static_cast<int>(journal::Level::Info));
    filter_->addItem(tr("Warnings and errors"), static_cast<int>(journal::Level::Warning));
    filter_->addItem(tr("Errors"), static_cast<int>(journal::Level::Error));
    filter_->setCurrentIndex(1);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Show:"), this));
    top->addWidget(filter_);
    top->addStretch(1);

    table_ = new QTableWidget(0, 3, this);
    table_->setHorizontalHeaderLabels({tr("Time"), tr("Kind"), tr("Record")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setWordWrap(false);
    table_->setAlternatingRowColors(true);

    auto* copy = new QPushButton(tr("Copy"), this);
    copy->setToolTip(tr("Copy the shown records — e.g. to attach to a bug report"));
    auto* folder = new QPushButton(tr("Open file folder"), this);
    folder->setToolTip(QDir::toNativeSeparators(journal::filePath()));
    auto* clear = new QPushButton(tr("Clear"), this);
    clear->setToolTip(tr("Clear the window (the file keeps the records)"));
    auto* close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(copy);
    buttons->addWidget(folder);
    buttons->addWidget(clear);
    buttons->addStretch(1);
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addLayout(top);
    layout->addWidget(table_, 1);
    layout->addLayout(buttons);

    connect(filter_, &QComboBox::currentIndexChanged, this, &JournalDialog::fill);
    connect(journal::notifier(), &journal::Notifier::added, this, &JournalDialog::append);
    connect(journal::notifier(), &journal::Notifier::cleared, this, &JournalDialog::fill);
    connect(copy, &QPushButton::clicked, this, [this] {
        QStringList lines;
        for (int row = 0; row < table_->rowCount(); ++row) {
            lines << table_->item(row, 0)->data(Qt::UserRole).toString() + '\t' + table_->item(row, 1)->text() +
                         '\t' + table_->item(row, 2)->data(Qt::UserRole).toString();
        }
        QApplication::clipboard()->setText(lines.join('\n'));
    });
    connect(folder, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(journal::filePath()).absolutePath()));
    });
    connect(clear, &QPushButton::clicked, this, [] { journal::clear(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    fill();
    resize(860, 560);
}

void JournalDialog::fill() {
    table_->setRowCount(0);
    for (const journal::Entry& entry : journal::entries()) {
        append(entry);
    }
}

void JournalDialog::append(const journal::Entry& entry) {
    if (static_cast<int>(entry.level) > filter_->currentData().toInt()) {
        return;
    }
    const bool atEnd = table_->rowCount() == 0 || table_->verticalScrollBar() == nullptr ||
                       table_->verticalScrollBar()->value() == table_->verticalScrollBar()->maximum();
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto* time = new QTableWidgetItem(entry.time.toString(QStringLiteral("dd.MM HH:mm:ss")));
    time->setData(Qt::UserRole, entry.time.toString(Qt::ISODateWithMs));
    auto* kind = new QTableWidgetItem(levelText(entry.level));
    // Многострочная запись — первой строкой, целиком — в подсказке.
    auto* text = new QTableWidgetItem(entry.text.section('\n', 0, 0) + (entry.text.contains('\n') ? " …" : ""));
    text->setData(Qt::UserRole, entry.text);
    text->setToolTip(entry.text);
    if (entry.level == journal::Level::Error) {
        kind->setForeground(theme::color(theme::Color::Danger));
    } else if (entry.level == journal::Level::Warning) {
        kind->setForeground(theme::color(theme::Color::Warning));
    } else if (entry.level == journal::Level::Debug) {
        kind->setForeground(theme::color(theme::Color::TextMuted));
    }
    table_->setItem(row, 0, time);
    table_->setItem(row, 1, kind);
    table_->setItem(row, 2, text);
    if (atEnd) {
        table_->scrollToBottom();
    }
}

} // namespace harmony::app
