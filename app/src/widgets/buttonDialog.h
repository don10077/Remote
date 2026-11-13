#pragma once

/**
 * @file buttonDialog.h
 * @brief Окно кнопки активности или устройства: какая кнопка, кому и какую команду шлёт.
 */

#include "buttonRef.h"
#include "harmony/core/buttonOwner.h"
#include "harmony/core/macro.h"
#include "harmony/core/model/remoteConfig.h"

#include <vector>

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace harmony::app {

/// Назначение аппаратной кнопки или правка экранной.
class ButtonDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно.
     * @param config Конфигурация (устройства и их команды).
     * @param owner Чьи кнопки: активности (команды её устройствам) или устройства (только ему).
     * @param button Кнопка: аппаратная (пустое имя — выбрать), экранная (-1 — новая).
     * @param parent Родительское окно.
     */
    ButtonDialog(const model::RemoteConfig& config, const ButtonOwner& owner, ButtonRef button,
                 QWidget* parent = nullptr, Macro initialMacro = {});

    /// @return true — кнопка выполняет несколько шагов (макрос), см. macro().
    [[nodiscard]] bool macroChosen() const;

    /// @return Шаги макроса.
    [[nodiscard]] const Macro& macro() const { return macro_; }

    /// @return Выбранная кнопка (у аппаратной — возможно, другая, чем при открытии).
    [[nodiscard]] ButtonRef button() const;

    /// @return Подпись экранной кнопки (пусто — оставить значок или имя команды).
    [[nodiscard]] std::string label() const;

    /// @return Значок экранной кнопки (пусто — без значка, с подписью).
    [[nodiscard]] std::string icon() const;

    /// @return Устройство, которому кнопка шлёт команду.
    [[nodiscard]] std::string deviceId() const;

    /// @return Команда устройства.
    [[nodiscard]] std::string command() const;

    /// @return true — пользователь нажал «Снять назначение» / «Удалить кнопку».
    [[nodiscard]] bool removeChosen() const { return remove_; }

private:
    /// Заполняет список команд выбранного устройства с учётом фильтра.
    void fillCommands();

    /// Обновляет подсказку о текущем назначении и доступность «Готово».
    void updateState();

    /// Действие кнопки сейчас (пусто — не назначена).
    [[nodiscard]] std::string currentAction(const ButtonRef& button) const;

    const model::RemoteConfig& config_;
    const std::vector<model::ControlGroup>* groups_ = nullptr; ///< Кнопки владельца
    std::vector<std::string> devices_;                         ///< Кому можно слать команды
    ButtonRef initial_;
    bool remove_ = false;
    std::string selectedCommand_; ///< Команда для выделения после перезаполнения списка

    QComboBox* key_ = nullptr;
    QLineEdit* label_ = nullptr;
    QComboBox* icon_ = nullptr; ///< Значок экранной кнопки
    QComboBox* device_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QListWidget* commands_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* ok_ = nullptr;

    // Макрос: шаги по порядку и их редактор.
    /// Перерисовывает список шагов (выделенным остаётся @p select).
    void fillSteps(int select);
    [[nodiscard]] QString stepText(const MacroStep& step) const;

    Macro macro_;
    QRadioButton* single_ = nullptr;
    QRadioButton* multi_ = nullptr;
    QWidget* macroBox_ = nullptr;
    QListWidget* steps_ = nullptr;
    QSpinBox* pause_ = nullptr;
};

} // namespace harmony::app
