#pragma once

/**
 * @file copyButtonsDialog.h
 * @brief Окно «Копировать кнопки»: раскладка или одна кнопка — в другие активности.
 */

#include "buttonRef.h"
#include "harmony/core/buttonCopy.h"
#include "harmony/core/model/remoteConfig.h"

#include <QDialog>

#include <optional>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;

namespace harmony::app {

/// Копирование кнопок активности в другие активности.
class CopyButtonsDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param config Конфигурация (живёт дольше окна).
     * @param activityId Откуда копировать.
     * @param selected Выбранная кнопка (можно скопировать только её); nullopt — нет.
     * @param parent Родительское окно.
     */
    CopyButtonsDialog(const model::RemoteConfig& config, const std::string& activityId,
                      std::optional<ButtonRef> selected, QWidget* parent = nullptr);

    /// @return Что и куда копировать (после Accepted).
    [[nodiscard]] ButtonCopy copy() const;

private:
    void updateState();

    std::string activityId_;
    std::optional<ButtonRef> selected_;
    int hardCount_ = 0; ///< Аппаратных кнопок у исходной активности
    int softCount_ = 0; ///< Экранных

    QRadioButton* onlySelected_ = nullptr;
    QRadioButton* layout_ = nullptr;
    QCheckBox* hard_ = nullptr;
    QCheckBox* soft_ = nullptr;
    QListWidget* targets_ = nullptr;
    QRadioButton* replace_ = nullptr;
    QRadioButton* keep_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* copy_ = nullptr;
};

} // namespace harmony::app
