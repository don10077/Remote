/**
 * @file buttonIcons.cpp
 * @brief Имена значков экранных кнопок по семейству модели (U6).
 */

#include "harmony/core/buttonIcons.h"

#include "harmony/core/remoteFamily.h"

#include <algorithm>

namespace harmony {

namespace {

// Прежний общий список — данные arch 15 (900). Он не проверен для других моделей, поэтому
// используется только когда модель не выбрана или семейство не описано.
constexpr int kUnverifiedFamily = 15;

} // namespace

std::span<const std::string_view> buttonIcons(const ModelInfo& model) {
    if (!model.id.empty()) {
        if (const auto* family = remoteFamily(model.architecture); family) {
            return family->buttonIcons;
        }
    }
    // Модель не выбрана (или семейство не описано) — прежний общий список, то есть данные
    // arch 15; он не проверен для этой модели.
    const auto* general = remoteFamily(kUnverifiedFamily);
    return general ? general->buttonIcons : std::span<const std::string_view>{};
}

bool isKnownButtonIcon(const ModelInfo& model, std::string_view name) {
    const auto names = buttonIcons(model);
    return std::ranges::find(names, name) != names.end();
}

} // namespace harmony
