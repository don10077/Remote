#pragma once

/**
 * @file buttonRef.h
 * @brief Ссылка на кнопку активности: аппаратную (по имени) или экранную (по месту).
 */

#include <QString>

#include <string>
#include <utility>

namespace harmony::app {

/// Кнопка активности.
struct ButtonRef {
    bool soft = false; ///< true — кнопка на экране пульта, false — аппаратная
    QString key;       ///< Аппаратная кнопка («Menu»); пусто — ещё не выбрана
    int position = -1; ///< Место экранной кнопки; -1 — новая (на первое свободное место)

    /// Аппаратная кнопка (пустое имя — выбрать в окне).
    static ButtonRef hard(const QString& name) { return {false, name, -1}; }

    /// Экранная кнопка на месте @p place (-1 — новая).
    static ButtonRef screen(int place) { return {true, {}, place}; }
};

/**
 * @brief Разбирает действие кнопки «<устройство>_<команда>_<модификатор>».
 * @return Пара «устройство, команда»; пустые строки, если формат другой.
 */
inline std::pair<std::string, std::string> splitActionId(const std::string& actionId) {
    const auto first = actionId.find('_');
    const auto last = actionId.rfind('_');
    if (first == std::string::npos || last <= first) {
        return {};
    }
    return {actionId.substr(0, first), actionId.substr(first + 1, last - first - 1)};
}

} // namespace harmony::app
