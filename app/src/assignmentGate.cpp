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
                       [&](const harmony::ModelKey& key) {
                           return key.name == name && key.source != harmony::KeySource::Config;
                       });
}

std::size_t outsideModelAssignmentCount(const harmony::ModelKeys& keys,
                                        const harmony::model::RemoteConfig& config) {
    std::size_t count = 0;
    const auto add = [&](const auto& owners) {
        for (const auto& owner : owners)
            for (const auto& group : owner.controlGroups)
                for (const auto& button : group.buttons)
                    if (!button.name.empty() && !button.actionId.empty() &&
                        !buttonAllowed(ButtonRef::hard(QString::fromStdString(button.name)), keys,
                                       ButtonAction::AssignTo, false)) ++count;
    };
    add(config.devices);
    add(config.activities);
    return count;
}

} // namespace harmony::app
