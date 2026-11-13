#pragma once

/**
 * @file helpDialog.h
 * @brief Окно справки: оглавление, поиск и текст раздела.
 */

#include "../help.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QTextBrowser;

namespace harmony::app {

/// Немодальное окно справки. Создаётся через help::show().
class HelpDialog final : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(QWidget* parent = nullptr);

    /// Открывает раздел (сбрасывает поиск, если раздел им скрыт).
    void setTopic(help::Topic topic);

protected:
    /// Смена темы: текст перерисовывается с новыми цветами.
    void changeEvent(QEvent* event) override;

private:
    /// Показывает текст выбранного в оглавлении раздела.
    void render();

    /// Скрывает разделы без искомого текста и подсвечивает первое совпадение.
    void filter(const QString& text);

    /// Ищет следующее совпадение в тексте раздела (по кругу).
    void findNext();

    /// Переход по ссылке: на раздел справки или во внешний браузер.
    void openLink(const QUrl& url);

    QLineEdit* search_ = nullptr;
    QListWidget* contents_ = nullptr;
    QTextBrowser* text_ = nullptr;
};

} // namespace harmony::app
