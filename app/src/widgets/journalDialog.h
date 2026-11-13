#pragma once

/**
 * @file journalDialog.h
 * @brief Окно «Журнал»: записи программы с фильтром, копированием и папкой файла.
 */

#include "../journal.h"

#include <QDialog>

class QComboBox;
class QTableWidget;

namespace harmony::app {

/// Журнал программы; новые записи появляются сразу.
class JournalDialog final : public QDialog {
    Q_OBJECT

public:
    explicit JournalDialog(QWidget* parent = nullptr);

private:
    /// Заполняет таблицу заново по фильтру.
    void fill();

    /// Добавляет строку, если она проходит фильтр.
    void append(const journal::Entry& entry);

    QComboBox* filter_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace harmony::app
