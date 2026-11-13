#pragma once

/**
 * @file buttonOwner.h
 * @brief Чьи кнопки правятся: активности или устройства.
 *
 * У активности кнопки шлют команды устройствам, которые она включает.
 * У устройства (режим «Устройства» на пульте) — только ему самому.
 */

#include <string>
#include <utility>

namespace harmony {

/// Владелец кнопок.
struct ButtonOwner {
    enum class Kind { Activity, Device };

    Kind kind = Kind::Activity;
    std::string id; ///< Id активности или устройства

    /// Кнопки активности.
    static ButtonOwner activity(std::string activityId) { return {Kind::Activity, std::move(activityId)}; }

    /// Кнопки устройства.
    static ButtonOwner device(std::string deviceId) { return {Kind::Device, std::move(deviceId)}; }

    [[nodiscard]] bool isDevice() const { return kind == Kind::Device; }

    bool operator==(const ButtonOwner&) const = default;
};

} // namespace harmony
