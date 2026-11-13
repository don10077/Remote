#pragma once

/**
 * @file replaceDeviceDialog.h
 * @brief Окно «Заменить устройство»: новое устройство на место прежнего в активностях.
 *
 * Выбирается новое устройство и активности; таблица показывает команды
 * прежнего, которые используют кнопки и шаги запуска, и что им соответствует
 * у нового (подобрано по имени и синонимам, можно поменять или убрать).
 */

#include "harmony/core/configDocument.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace harmony::app {

/// Замена устройства в активностях.
class ReplaceDeviceDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param document Конфигурация (живёт дольше окна; не меняется).
     * @param deviceId Прежнее устройство.
     * @param parent Родительское окно.
     */
    ReplaceDeviceDialog(const ConfigDocument& document, const std::string& deviceId, QWidget* parent = nullptr);

    /// @return Что заменить (после Accepted).
    [[nodiscard]] DeviceReplacement replacement() const;

private:
    /// Заполняет таблицу команд по новому устройству и выбранным активностям.
    void fillCommands();

    /// Сводка и доступность кнопки.
    void updateSummary();

    [[nodiscard]] std::vector<std::string> chosenActivities() const;

    const ConfigDocument& document_;
    std::string oldId_;

    QComboBox* target_ = nullptr;
    QListWidget* activities_ = nullptr;
    QTableWidget* commands_ = nullptr;
    QCheckBox* removeOld_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* replace_ = nullptr;
};

} // namespace harmony::app
