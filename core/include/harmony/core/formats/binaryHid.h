#pragma once

#include "harmony/core/error.h"
#include "harmony/core/model/remoteConfig.h"
#include <cstdint>
#include <span>

namespace harmony::formats {
/**
 * @brief Imports devices, activities, commands and button bindings from a binary HID image.
 * @param payload Verified EZHex payload, without its XML envelope.
 * @return Normalised editor model; malformed or unsupported input returns an error.
 * Implemented in the native core; configuration programs are never executed.
 */
[[nodiscard]] Result<model::RemoteConfig> importBinaryHid(std::span<const std::uint8_t> payload);
}
