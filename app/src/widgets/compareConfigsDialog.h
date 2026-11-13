#pragma once

/**
 * @file compareConfigsDialog.h
 * @brief Окно «Сравнить с другой конфигурацией»: чем открытая отличается от выбранной.
 *
 * Другая — из недавних или любой файл (.EZHex, UserConfiguration.xml):
 * устройства и активности, которые есть только в одной из них, и что
 * отличается у общих (команды, кнопки, состав, входы).
 */

#include "harmony/core/configDocument.h"

#include <QDialog>

#include <optional>

class QComboBox;
class QLabel;
class QTreeWidget;

namespace harmony::app {

/// Сравнение открытой конфигурации с другой.
class CompareConfigsDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param current Открытая конфигурация (живёт дольше окна).
     * @param parent Родительское окно.
     */
    explicit CompareConfigsDialog(const ConfigDocument& current, QWidget* parent = nullptr);

    /// Сравнивает с конфигурацией по пути (папка или файл); для кнопки «Обзор…» и снимков.
    void compareWith(const QString& path);

private:
    void browse();
    void showDifferences();

    const ConfigDocument& current_;
    std::optional<ConfigDocument> other_;

    QComboBox* source_ = nullptr;
    QLabel* summary_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};

} // namespace harmony::app
