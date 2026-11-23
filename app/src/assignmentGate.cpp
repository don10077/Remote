/**
 * @file assignmentGate.cpp
 * @brief Гейт назначения кнопки по правилу блока A (шаг A1).
 *
 * Гейт — «есть документ». Список клавиш модели больше ничего не запрещает: он нужен только
 * там, где решается, на какую клавишу можно назначить.
 */

#include "assignmentGate.h"

#include <algorithm>

namespace harmony::app {

bool gateOpen(bool hasDocument) {
    return hasDocument;
}

bool buttonAllowed(const ButtonRef& button, const harmony::ModelKeys& keys,
                   ButtonAction action, bool emptyNameAllowed) {
    if (button.soft) {
        return true;   // экранная кнопка: место проверяется на месте вызова, как прежде
    }
    if (button.key.isEmpty()) {
        return emptyNameAllowed;
    }
    // Снять или перенести назначение с клавиши можно и вне списка: иначе предупреждение C1
    // нельзя исправить (Q5).
    if (action == ButtonAction::TakeFrom) {
        return true;
    }
    // Назначить можно только клавишу модели, когда список исчерпывающий. Неполный список
    // (в том числе запасной путь к словарю) запретом не является.
    if (!keys.complete) {
        return true;
    }
    const std::string name = button.key.toStdString();
    return std::any_of(keys.keys.begin(), keys.keys.end(),
                       [&](const harmony::ModelKey& key) { return key.name == name; });
}

} // namespace harmony::app
