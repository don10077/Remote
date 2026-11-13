#pragma once

/**
 * @file activityTemplate.h
 * @brief Внутреннее: свойства новой активности по её типу.
 */

#include <string>
#include <utility>
#include <vector>

namespace harmony::detail {

/**
 * @brief Свойства (<Properties>) новой активности, как их пишет пульт для этого типа.
 * @param type Тип активности; незнакомый — как «Просмотр ТВ».
 */
[[nodiscard]] const std::vector<std::pair<std::string, std::string>>& activityProperties(
    const std::string& type);

} // namespace harmony::detail
