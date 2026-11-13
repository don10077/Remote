#pragma once

/**
 * @file compareDevicesDialog.h
 * @brief Окно «Сравнить устройства»: команды двух устройств рядом, копирование отмеченных.
 *
 * Строки — команды обоих устройств по имени: одинаковые, разные, только у
 * одного. Отмеченные (те, что есть у левого) копируются правому; «⇄» меняет
 * устройства местами.
 */

#include "harmony/core/configDocument.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace harmony::app {

/// Сравнение команд двух устройств.
class CompareDevicesDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param document Конфигурация (живёт дольше окна; не меняется).
     * @param deviceId Устройство, с которого открыто окно (слева).
     * @param parent Родительское окно.
     */
    CompareDevicesDialog(const ConfigDocument& document, const std::string& deviceId, QWidget* parent = nullptr);

    /// @return Откуда копировать (после Accepted).
    [[nodiscard]] std::string fromId() const { return first_; }

    /// @return Куда копировать.
    [[nodiscard]] std::string toId() const;

    /// @return Отмеченные команды.
    [[nodiscard]] std::vector<std::string> marked() const;

private:
    /// Заполняет таблицу заново.
    void fill();

    /// Сводка и подпись кнопки копирования.
    void updateSummary();

    [[nodiscard]] QString nameOf(const std::string& id) const;

    const ConfigDocument& document_;
    std::string first_; ///< Левое устройство (откуда копировать)

    QLabel* title_ = nullptr;
    QComboBox* second_ = nullptr;
    QCheckBox* onlyDifferences_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* markMissing_ = nullptr;
    QPushButton* copy_ = nullptr;
};

} // namespace harmony::app
