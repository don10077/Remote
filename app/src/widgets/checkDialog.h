#pragma once

/**
 * @file checkDialog.h
 * @brief Окно «Проверка конфигурации»: что на пульте не сработает, и исправление.
 *
 * Список проблем ConfigDocument::check(): сначала ошибки (кнопка не сработает),
 * затем предупреждения. «Исправить» — выбранную, «Исправить всё» — всё, что
 * исправляется само; каждое исправление — шаг отмены главного окна.
 * «Показать» закрывает окно и открывает место проблемы.
 */

#include "harmony/core/configDocument.h"

#include <QDialog>
#include <functional>
#include <optional>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace harmony::app {

/// Проверка конфигурации.
class CheckDialog final : public QDialog {
    Q_OBJECT

public:
    /// Правка документа с шагом отмены (MainWindow::applyEdit).
    using Apply = std::function<bool(const QString& text,
                                     const std::function<Result<void>(ConfigDocument&)>& change)>;

    /**
     * @brief Создаёт окно и проверяет конфигурацию.
     * @param document Открытая конфигурация (живёт дольше окна; правится только через @p apply).
     * @param apply Правка с отменой.
     * @param parent Родительское окно.
     */
    CheckDialog(const ConfigDocument& document, Apply apply, QWidget* parent = nullptr);

    /// @return Проблема, место которой попросили показать (окно закрыто с Accepted).
    [[nodiscard]] const std::optional<Problem>& target() const { return target_; }

private:
    /// Проверяет заново и заполняет список.
    void refresh();

    /// Обновляет кнопки по выбору.
    void updateButtons();

    /// Выбранная проблема; nullptr — не выбрана.
    [[nodiscard]] const Problem* selected() const;

    const ConfigDocument& document_;
    Apply apply_;
    std::vector<Problem> problems_;
    std::optional<Problem> target_;

    QLabel* summary_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPushButton* show_ = nullptr;
    QPushButton* fix_ = nullptr;
    QPushButton* fixAll_ = nullptr;
};

} // namespace harmony::app
