/**
 * @file assignmentGate.cpp
 * @brief Гейт назначения кнопки: пустой список клавиш модели — действия запрещены.
 */

#include "assignmentGate.h"

namespace harmony::app {

bool assignmentAllowed(const QStringList& keys)
{
    return !keys.isEmpty();
}

} // namespace harmony::app
