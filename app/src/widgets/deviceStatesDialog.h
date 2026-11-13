#pragma once

/**
 * @file deviceStatesDialog.h
 * @brief Окна «Питание» и «Входы»: как пульт включает и выключает устройство и переключает входы.
 *
 * Одно окно с двумя разделами; открывается на одном из них (кнопки «Питание…» и
 * «Входы…» на странице устройства), второй раздел не показывается и не меняется.
 * Питание: вручную / отдельные «вкл» и «выкл» / одна кнопка; пауза после включения.
 * Входы: нет / у каждого своя команда / по кругу одной командой; список входов.
 * Действие — обычно одна команда (выбор в списке); «…» открывает шаги целиком:
 * несколько команд, паузы, шаги Logitech (перевод вспомогательного состояния).
 * «Заполнить по командам» — заготовка по именам команд (PowerOn, InputHdmi1…).
 */

#include "harmony/core/deviceStates.h"
#include "harmony/core/model/remoteConfig.h"

#include <QDialog>

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace harmony::app {

/**
 * @brief Действие: одна команда в списке или несколько шагов («…» — правка шагов).
 */
class SequenceEdit final : public QWidget {
    Q_OBJECT

public:
    /**
     * @param commands Команды устройства.
     * @param optional Пусто — допустимо (первый пункт «—»).
     */
    SequenceEdit(const QStringList& commands, bool optional, QWidget* parent = nullptr);

    void setSequence(const StateSequence& steps);
    [[nodiscard]] const StateSequence& sequence() const { return steps_; }

signals:
    void changed();

private:
    /// Показывает шаги в списке.
    void refresh();

    QStringList commands_;
    bool optional_;
    StateSequence steps_;
    QComboBox* combo_ = nullptr;
    QToolButton* more_ = nullptr;
};

/// Окно «Питание» или «Входы».
class DeviceStatesDialog final : public QDialog {
    Q_OBJECT

public:
    /// Какой раздел правится.
    enum class Section {
        Power,  ///< Включение и выключение
        Inputs, ///< Переключение входов
    };

    /**
     * @param device Устройство (команды — для выбора).
     * @param states Текущие питание и входы.
     * @param section Раздел, который показывается; другой остаётся как был.
     * @param parent Родительское окно.
     */
    DeviceStatesDialog(const model::Device& device, const DeviceStates& states, Section section,
                       QWidget* parent = nullptr);

    /// @return Что задали (после Accepted).
    [[nodiscard]] DeviceStates states() const;

private:
    /// Заполняет поля.
    void load(const DeviceStates& states);

    /// Показывает поля выбранных режимов.
    void updateVisibility();

    /// Добавляет строку входа.
    void addInputRow(const InputSetup::Input& input);

    /// Проверяет заполнение; пусто — всё в порядке.
    [[nodiscard]] QString problem() const;

    QStringList commands_;
    std::string deviceType_;
    Section section_;

    QButtonGroup* powerMode_ = nullptr;
    QWidget* powerDiscrete_ = nullptr;
    QWidget* powerToggle_ = nullptr;
    SequenceEdit* powerOn_ = nullptr;
    SequenceEdit* powerOff_ = nullptr;
    SequenceEdit* toggle_ = nullptr;
    QDoubleSpinBox* powerDelay_ = nullptr;
    QWidget* powerDelayRow_ = nullptr;

    QButtonGroup* inputMode_ = nullptr;
    QWidget* inputBox_ = nullptr;
    QTableWidget* inputs_ = nullptr;
    QWidget* cycle_ = nullptr;
    SequenceEdit* next_ = nullptr;
    SequenceEdit* prev_ = nullptr;
    QDoubleSpinBox* inputDelay_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* save_ = nullptr;
};

} // namespace harmony::app
