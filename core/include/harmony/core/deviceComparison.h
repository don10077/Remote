#pragma once

/**
 * @file deviceComparison.h
 * @brief Сравнение команд двух устройств.
 *
 * Команды сопоставляются по имени. Одноимённые считаются одинаковыми, если
 * совпадает запись (протокол и код) или сам сигнал — по ir::sameSignal().
 */

#include <string>
#include <vector>

namespace harmony {

/// Команда в сравнении двух устройств.
struct CommandComparison {
    /// Чем отличается.
    enum class Status {
        Same,       ///< Есть у обоих, сигнал тот же
        Different,  ///< Есть у обоих, сигналы разные
        OnlyFirst,  ///< Только у первого
        OnlySecond, ///< Только у второго
    };

    std::string name;   ///< Имя команды
    Status status = Status::Same;
    std::string first;  ///< Сигнал у первого: «NEC · address 0x04, command 0x08» / «learned» (пусто — нет)
    std::string second; ///< То же у второго
};

} // namespace harmony
