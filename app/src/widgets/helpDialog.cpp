/**
 * @file helpDialog.cpp
 * @brief Окно справки.
 */

#include "helpDialog.h"

#include "../theme.h"

#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

namespace harmony::app {

namespace {

/// Схема ссылок на разделы справки: `topic:<номер>`.
constexpr const char* kTopicScheme = "topic";

/// @return Таблица стилей текста справки в цветах текущей темы.
QString documentStyle() {
    const QString accent = theme::color(theme::Color::Accent).name();
    const QString muted = theme::color(theme::Color::TextMuted).name();
    const QString border = theme::color(theme::Color::Border).name();
    return QStringLiteral("a { color: %1; text-decoration: none; }"
                          "h2 { margin-top: 0; }"
                          "h3 { margin-top: 14px; margin-bottom: 4px; }"
                          "p, li { line-height: 135%; }"
                          ".note { color: %2; }"
                          "td { padding: 3px 12px 3px 0; border-bottom: 1px solid %3; }")
        .arg(accent, muted, border);
}

/// @return Текст раздела без разметки — для поиска.
QString plainText(help::Topic topic) {
    QTextDocument document;
    document.setHtml(help::html(topic));
    return document.toPlainText();
}

} // namespace

HelpDialog::HelpDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Help"));
    setMinimumSize(760, 480);
    // Справку держат открытой рядом с окном программы, поэтому без кнопки «?» в заголовке.
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search help"));
    search_->setClearButtonEnabled(true);

    contents_ = new QListWidget(this);
    for (const help::Topic topic : help::topics()) {
        auto* item = new QListWidgetItem(help::title(topic), contents_);
        item->setData(Qt::UserRole, static_cast<int>(topic));
    }

    auto* left = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(search_);
    leftLayout->addWidget(contents_, 1);

    text_ = new QTextBrowser(this);
    text_->setOpenLinks(false);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(text_);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 640});
    splitter->setChildrenCollapsible(false);

    auto* close = new QPushButton(tr("Close"), this);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);

    connect(contents_, &QListWidget::currentRowChanged, this, &HelpDialog::render);
    connect(search_, &QLineEdit::textChanged, this, &HelpDialog::filter);
    connect(search_, &QLineEdit::returnPressed, this, &HelpDialog::findNext);
    connect(text_, &QTextBrowser::anchorClicked, this, &HelpDialog::openLink);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    contents_->setCurrentRow(0);
    resize(900, 620);
}

void HelpDialog::setTopic(help::Topic topic) {
    for (int row = 0; row < contents_->count(); ++row) {
        QListWidgetItem* item = contents_->item(row);
        if (item->data(Qt::UserRole).toInt() != static_cast<int>(topic)) {
            continue;
        }
        if (item->isHidden()) {
            search_->clear(); // filter() вернёт все разделы
        }
        if (contents_->currentRow() == row) {
            render(); // ссылка на открытый раздел: вернуться к началу
        } else {
            contents_->setCurrentRow(row);
        }
        return;
    }
}

void HelpDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        render();
    }
    QDialog::changeEvent(event);
}

void HelpDialog::render() {
    // render() зовётся и из changeEvent во время конструктора, когда виджетов ещё нет.
    if (text_ == nullptr || contents_ == nullptr) {
        return;
    }
    const QListWidgetItem* item = contents_->currentItem();
    if (item == nullptr) {
        text_->clear();
        return;
    }
    const auto topic = static_cast<help::Topic>(item->data(Qt::UserRole).toInt());
    text_->document()->setDefaultStyleSheet(documentStyle());
    text_->setHtml(help::html(topic));
    if (!search_->text().trimmed().isEmpty()) {
        findNext();
    }
}

void HelpDialog::filter(const QString& text) {
    const QString needle = text.trimmed();
    int firstVisible = -1;
    for (int row = 0; row < contents_->count(); ++row) {
        QListWidgetItem* item = contents_->item(row);
        const auto topic = static_cast<help::Topic>(item->data(Qt::UserRole).toInt());
        const bool match = needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive) ||
                           plainText(topic).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match && firstVisible < 0) {
            firstVisible = row;
        }
    }
    const QListWidgetItem* current = contents_->currentItem();
    if (current != nullptr && !current->isHidden()) {
        render(); // тот же раздел: перейти к первому совпадению
    } else if (firstVisible >= 0) {
        contents_->setCurrentRow(firstVisible);
    } else {
        text_->setHtml(QStringLiteral("<p>%1</p>").arg(tr("Nothing found.").toHtmlEscaped()));
    }
}

void HelpDialog::findNext() {
    const QString needle = search_->text().trimmed();
    if (needle.isEmpty()) {
        return;
    }
    if (!text_->find(needle)) {
        text_->moveCursor(QTextCursor::Start);
        text_->find(needle);
    }
}

void HelpDialog::openLink(const QUrl& url) {
    if (url.scheme() == QLatin1String(kTopicScheme)) {
        bool ok = false;
        const int index = url.path().toInt(&ok);
        if (ok) {
            for (const help::Topic topic : help::topics()) {
                if (static_cast<int>(topic) == index) {
                    setTopic(topic);
                    return;
                }
            }
        }
        return;
    }
    if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) {
        QDesktopServices::openUrl(url);
    }
}

} // namespace harmony::app
