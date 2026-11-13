/**
 * @file welcomePage.cpp
 * @brief Реализация стартового экрана.
 */

#include "welcomePage.h"

#include "../icons.h"
#include "../theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace harmony::app {

namespace {

/**
 * @brief Карточка-действие: рамка с содержимым, нажимается целиком.
 *
 * Сделана на QFrame, а не на QPushButton: у кнопки высота не подстраивается
 * под вложенные надписи, и пояснение в две строки обрезалось.
 */
class Card final : public QFrame {
public:
    /**
     * @param onClick Что делать по щелчку или по Enter/пробелу.
     * @param parent Родитель.
     */
    Card(std::function<void()> onClick, QWidget* parent) : QFrame(parent), onClick_(std::move(onClick)) {
        setObjectName("welcomeCard");
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus); // доступна с клавиатуры (Tab)
        setAttribute(Qt::WA_Hover);      // для :hover в таблице стилей
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            onClick_();
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            onClick_();
            return;
        }
        QFrame::keyPressEvent(event);
    }

private:
    std::function<void()> onClick_;
};

/**
 * @brief Создаёт карточку: значок, заголовок и пояснение.
 * @param icon Значок слева.
 * @param title Заголовок действия.
 * @param hint Пояснение (переносится на вторую строку при необходимости).
 * @param onClick Действие по нажатию.
 * @param parent Родитель.
 */
Card* makeCard(icons::Icon icon, const QString& title, const QString& hint,
               std::function<void()> onClick, QWidget* parent) {
    auto* card = new Card(std::move(onClick), parent);

    auto* iconLabel = new QLabel(card);
    iconLabel->setPixmap(icons::make(icon, theme::color(theme::Color::Accent))
                             .pixmap(QSize(26, 26), card->devicePixelRatioF()));
    iconLabel->setFixedSize(32, 32);
    iconLabel->setAlignment(Qt::AlignCenter);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName("welcomeCardTitle");

    auto* hintLabel = new QLabel(hint, card);
    hintLabel->setObjectName("welcomeHint");
    hintLabel->setWordWrap(true);

    auto* text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(3);
    text->addWidget(titleLabel);
    text->addWidget(hintLabel);

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(16, 14, 18, 14);
    row->setSpacing(14);
    row->addWidget(iconLabel, 0, Qt::AlignTop);
    row->addLayout(text, 1);
    return card;
}

} // namespace

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    setObjectName("welcome");
    setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel(tr("Harmony"), this);
    title->setObjectName("welcomeTitle");

    auto* subtitle = new QLabel(
        tr("Configure Logitech Harmony remotes without Logitech's cloud "
           "software."), this);
    subtitle->setObjectName("welcomeSubtitle");
    subtitle->setWordWrap(true);

    auto* create = makeCard(icons::Icon::Plus, tr("Create new"),
                            tr("An empty configuration — no remote needed."),
                            [this] { emit createRequested(); }, this);
    auto* choose = makeCard(icons::Icon::Folder, tr("Open configuration"),
                            tr("One of your configurations or a remote backup."),
                            [this] { emit chooseRequested(); }, this);
    auto* demo = makeCard(icons::Icon::Play, tr("View example"),
                          tr("A ready-made configuration — no remote or files needed."),
                          [this] { emit demoRequested(); }, this);
    auto* remote = makeCard(icons::Icon::Remote, tr("Connect remote"),
                            tr("The remote connects via USB and shows up as a network adapter."),
                            [this] { emit connectRequested(); }, this);

    // Колонка фиксированной ширины по центру окна.
    auto* column = new QWidget(this);
    column->setFixedWidth(560);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(10);
    columnLayout->addWidget(title);
    columnLayout->addWidget(subtitle);
    columnLayout->addSpacing(18);
    columnLayout->addWidget(create);
    columnLayout->addWidget(choose);
    columnLayout->addWidget(demo);
    columnLayout->addWidget(remote);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch(2);
    outer->addWidget(column, 0, Qt::AlignHCenter);
    outer->addStretch(3);
}

} // namespace harmony::app
