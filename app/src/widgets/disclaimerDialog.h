#pragma once

/**
 * @file disclaimerDialog.h
 * @brief Предупреждение о рисках при первом запуске: программа без гарантий, автор не отвечает за ущерб.
 */

#include <QDialog>

namespace harmony::app {

/// Окно с предупреждением; продолжить можно, только отметив «понимаю и принимаю».
class DisclaimerDialog final : public QDialog {
    Q_OBJECT

public:
    /// Версия текста. Увеличить, если текст существенно изменился, — окно покажется снова.
    static constexpr int kVersion = 1;

    explicit DisclaimerDialog(QWidget* parent = nullptr);

    /**
     * @brief Показывает окно, если текущая версия ещё не принята, и запоминает согласие.
     * @return false — пользователь отказался, программу надо закрыть.
     */
    [[nodiscard]] static bool ensureAccepted();
};

} // namespace harmony::app
