/**
 * @file appHeader.cpp
 * @brief Реализация шапки окна.
 */

#include "appHeader.h"

#include "../icons.h"
#include "../labels.h"
#include "../theme.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

namespace harmony::app {

namespace {

/// Плоская кнопка шапки со значком (отмена, повтор).
QToolButton* makeIconButton(icons::Icon icon, const QString& toolTip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setIcon(icons::make(icon));
    button->setIconSize(QSize(20, 20));
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    return button;
}

} // namespace

AppHeader::AppHeader(QMenu* connectionMenu, QWidget* parent)
    : QWidget(parent) {
    setObjectName("appHeader");
    setAttribute(Qt::WA_StyledBackground); // иначе фон из таблицы стилей не рисуется

    undoButton_ = makeIconButton(icons::Icon::Undo, tr("Undo (%1)").arg(labels::shortcut(QKeySequence::Undo)), this);
    redoButton_ = makeIconButton(icons::Icon::Redo, tr("Redo (%1)").arg(labels::shortcut(QKeySequence::Redo)), this);
    connect(undoButton_, &QToolButton::clicked, this, &AppHeader::undoRequested);
    connect(redoButton_, &QToolButton::clicked, this, &AppHeader::redoRequested);

    // --- Пульт: состояние + главное действие ---
    connectionButton_ = new QToolButton(this);
    connectionButton_->setObjectName("connectionButton");
    connectionButton_->setMenu(connectionMenu);
    connectionButton_->setPopupMode(QToolButton::InstantPopup);
    connectionButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connectionButton_->setIconSize(QSize(12, 12));

    writeButton_ = new QPushButton(icons::make(icons::Icon::Upload, theme::color(theme::Color::Surface)),
                                   tr("Write to remote"), this);
    writeButton_->setObjectName("primaryButton");
    writeButton_->setIconSize(QSize(18, 18));
    connect(writeButton_, &QPushButton::clicked, this, &AppHeader::writeRequested);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 12, 6);
    layout->setSpacing(4);
    layout->addWidget(undoButton_);
    layout->addWidget(redoButton_);
    layout->addStretch(1);
    auto* readButton = new QPushButton(tr("Read from remote"), this);
    connect(readButton, &QPushButton::clicked, this, &AppHeader::readRequested);
    layout->addWidget(readButton);
    layout->addWidget(connectionButton_);
    layout->addSpacing(8);
    layout->addWidget(writeButton_);

    setUndoState(false, {}, false, {});
    setConnectionState(false, tr("Remote not connected"));
}

void AppHeader::setUndoState(bool canUndo, const QString& undoText, bool canRedo,
                             const QString& redoText) {
    undoButton_->setEnabled(canUndo);
    redoButton_->setEnabled(canRedo);
    const QString undoKey = labels::shortcut(QKeySequence::Undo);
    const QString redoKey = labels::shortcut(QKeySequence::Redo);
    undoButton_->setToolTip(canUndo ? tr("Undo: %1 (%2)").arg(undoText, undoKey) : tr("Undo (%1)").arg(undoKey));
    redoButton_->setToolTip(canRedo ? tr("Redo: %1 (%2)").arg(redoText, redoKey) : tr("Redo (%1)").arg(redoKey));
}

void AppHeader::setConnectionState(bool connected, const QString& text) {
    const QColor dot = connected ? theme::color(theme::Color::Success)
                                 : theme::color(theme::Color::TextMuted);
    connectionButton_->setIcon(icons::make(icons::Icon::Dot, dot));
    connectionButton_->setText(text);
    // Записывать некуда, пока пульт не подключён, — кнопку не показываем вовсе,
    // чтобы не путать неактивной синей кнопкой.
    writeButton_->setVisible(connected);
}

void AppHeader::setWriteEnabled(bool writable) {
    writeButton_->setEnabled(writable);
}

} // namespace harmony::app
