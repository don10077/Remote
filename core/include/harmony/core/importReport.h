#pragma once

/**
 * @file importReport.h
 * @brief Отчёт импорта: предупреждения не должны притворяться свойствами конфигурации.
 */

#include <string>
#include <vector>

namespace harmony {

/**
 * @brief Предупреждения импорта.
 *
 * Они описывают, что формат не смог разобрать, но не принадлежат сохранённому проекту.
 * Документ хранит их рядом с моделью, чтобы показать после открытия.
 */
struct ImportReport {
    std::vector<std::string> warnings;
};

} // namespace harmony
