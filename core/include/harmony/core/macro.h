#pragma once

/**
 * @file macro.h
 * @brief Макрос: кнопка, которая выполняет несколько шагов подряд.
 *
 * Обычная кнопка ссылается на действие «<устройство>_<команда>_Hold» — одну команду.
 * Кнопка-макрос ссылается на своё действие в ActionLists.xml с несколькими <Action>:
 * @code
 *   <ActionList name="79016674_macro_1">
 *     <Action>…SendCommand DeviceId=92555930 Command=PowerToggle…</Action>
 *     <Action>…SendDelay DeviceId=92555930 Delay=2000…</Action>
 *     <Action>…SendCommand DeviceId=82969357 Command=InputHdmi1…</Action>
 *   </ActionList>
 * @endcode
 * Пульт выполняет все действия списка по порядку (HAO.lua, handleAction: SendCommand,
 * SendDelay, SetValue, SendNumber). Имя «<владелец>_macro_<N>» — наше; действия с
 * другими именами программа не создаёт и не удаляет.
 */

#include "harmony/core/deviceStates.h"

#include <string>
#include <string_view>
#include <vector>

namespace harmony {

/// Шаг макроса: шаг действия (команда, пауза, перевод состояния) и устройство.
struct MacroStep {
    std::string deviceId;
    StateStep step;

    bool operator==(const MacroStep&) const = default;
};

/// Шаги макроса по порядку.
using Macro = std::vector<MacroStep>;

/// @return true, если действие кнопки — макрос этой программы («<id>_macro_<N>»).
[[nodiscard]] bool isMacroAction(std::string_view actionId);

} // namespace harmony
