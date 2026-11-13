#pragma once

/**
 * @file deviceStates.h
 * @brief Питание и входы устройства: как пульт его включает, выключает и переключает входы.
 *
 * В UserConfiguration.xml это состояния устройства (<States>):
 * @code
 *   <State><Id>Power</Id><Value>Off</Value><Value>On</Value><Delay>8000</Delay>
 *     <DiscreteActions>                         ← отдельные «вкл» и «выкл»
 *       <SetAction><Action>…SendCommand PowerOff…</Action><Name>Off</Name></SetAction>
 *       <SetAction><Action>…SendCommand PowerOn…</Action><Name>On</Name></SetAction>
 *     </DiscreteActions>
 *     — или —
 *     <RelativeActions><NextAction><Action>…PowerToggle…</Action></NextAction></RelativeActions>
 *   </State>
 *   <State><Id>Input</Id><Value>HDMI1</Value>…<Delay>0</Delay>
 *     <DiscreteActions><SetAction>…<Name>HDMI1</Name></SetAction>…</DiscreteActions>   ← прямой
 * выбор — или —
 *     <RelativeActions><NextAction>…</NextAction><PrevAction>…</PrevAction></RelativeActions> ← по
 * кругу
 *   </State>
 * @endcode
 * Действие — последовательность шагов: команда (SendCommand, бывает с длительностью
 * нажатия), пауза (SendDelay), перевод другого состояния (SetValue — так Logitech
 * описывает, например, вход через вспомогательное состояние InputType).
 * Другие состояния устройства (InputType, TVInput…) редактор не трогает.
 */

#include <string>
#include <vector>

namespace harmony {

/// Шаг действия.
struct StateStep {
    enum class Kind {
        Command,  ///< Отправить команду устройства
        Delay,    ///< Пауза
        SetValue, ///< Перевести состояние устройства в значение
        Other, ///< Незнакомая операция — сохраняется как есть
    };
    Kind kind = Kind::Command;
    std::string command; ///< Command: имя команды
    int durationMs = 0; ///< Command: длительность нажатия (0 — обычное)
    int delayMs = 0;    ///< Delay: пауза, мс
    std::string state;  ///< SetValue: состояние
    std::string value;  ///< SetValue: значение
    std::string raw;    ///< Other: XML действия

    /// Шаг «нажать команду».
    static StateStep press(std::string name) {
        StateStep step;
        step.command = std::move(name);
        return step;
    }

    /// Шаг «пауза».
    static StateStep pause(int ms) {
        StateStep step;
        step.kind = Kind::Delay;
        step.delayMs = ms;
        return step;
    }

    bool operator==(const StateStep&) const = default;
};

/// Последовательность шагов.
using StateSequence = std::vector<StateStep>;

/// Питание устройства.
struct PowerSetup {
    enum class Mode {
        Manual, ///< Пульт не управляет питанием (включают руками)
        Discrete, ///< Отдельные команды «вкл» и «выкл»
        Toggle,   ///< Одна кнопка-переключатель
    };
    Mode mode = Mode::Manual;
    StateSequence on;     ///< Discrete
    StateSequence off;    ///< Discrete
    StateSequence toggle; ///< Toggle
    int delayMs = 0; ///< Сколько ждать после включения, прежде чем слать другие команды

    bool operator==(const PowerSetup&) const = default;
};

/// Входы устройства.
struct InputSetup {
    enum class Mode {
        None,     ///< Пульт не переключает входы
        Discrete, ///< У каждого входа своя команда
        Cycle,    ///< Переключение по кругу одной командой
    };
    /// Вход.
    struct Input {
        std::string name;     ///< «HDMI1»
        StateSequence select; ///< Discrete: как выбрать

        bool operator==(const Input&) const = default;
    };
    Mode mode = Mode::None;
    std::vector<Input> inputs; ///< По порядку (для Cycle — порядок переключения)
    StateSequence next; ///< Cycle: следующий вход
    StateSequence prev; ///< Cycle: предыдущий (может быть пустым)
    int delayMs = 0;    ///< Пауза после переключения

    bool operator==(const InputSetup&) const = default;
};

/// Питание и входы.
struct DeviceStates {
    PowerSetup power;
    InputSetup input;

    bool operator==(const DeviceStates&) const = default;
};

/**
 * @brief Заготовка по именам команд устройства («мастер»).
 *
 * Питание: PowerOn и PowerOff — отдельные, иначе PowerToggle (или Power) — переключатель.
 * Входы: команды Input<Имя> (InputHdmi1, InputAv…) — прямой выбор; иначе InputNext /
 * Input — по кругу. Задержки — 8 с для телевизоров и проекторов, 3 с для остальных.
 * @param commands Имена команд устройства.
 * @param deviceType Тип устройства («Television»…).
 */
[[nodiscard]] DeviceStates guessDeviceStates(const std::vector<std::string>& commands,
                                             const std::string& deviceType);

} // namespace harmony
