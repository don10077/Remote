#pragma once

/**
 * @file newDeviceDialog.h
 * @brief Окно «Новое устройство»: имя, тип, производитель и модель; команды выучиваются потом.
 */

#include "harmony/core/configDocument.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace harmony::app {

/// Данные нового устройства.
class NewDeviceDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно.
     * @param config Конфигурация: имя нового устройства не должно совпадать с имеющимися.
     * @param parent Родительское окно.
     */
    explicit NewDeviceDialog(const model::RemoteConfig& config, QWidget* parent = nullptr);

    /// @return Данные устройства.
    [[nodiscard]] DeviceDraft draft() const;

private:
    QLabel* nameProblem_ = nullptr; ///< «Такое имя уже есть»
    QLineEdit* label_ = nullptr;
    QComboBox* type_ = nullptr;
    QLineEdit* manufacturer_ = nullptr;
    QLineEdit* model_ = nullptr;
    QPushButton* create_ = nullptr;
};

} // namespace harmony::app
