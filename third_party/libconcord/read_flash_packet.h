// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Validates one classic HID flash response before accessing its payload.
 * @return Payload length, zero for a complete DONE, or -1 for an invalid packet.
 */
inline int harmony_flash_packet(const uint8_t* packet, std::size_t size,
                               unsigned protocol, uint8_t sequence,
                               std::size_t remaining) {
    if (size < 2) return -1;
    const auto kind = packet[0] & 0xf0;
    if (kind == 0xf0) return remaining == 0 ? 0 : -1;
    if (kind != 0x60 || packet[1] != sequence) return -1;
    static constexpr unsigned lengths[16] =
        {0, 0, 1, 2, 3, 4, 5, 6, 14, 30, 62, 0, 0, 0, 0, 0};
    const unsigned code = packet[0] & 0x0f;
    const unsigned count = protocol ? lengths[code] : (code >= 2 ? code - 1 : 0);
    if (!count || count > remaining || count + 2 > size) return -1;
    return static_cast<int>(count);
}
