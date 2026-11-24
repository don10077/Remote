// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include "protocol_z.h"

/**
 * @brief Checks a Z-HID TCP report before copying its header or payload.
 * @param packet Report including its length byte.
 * @param size Actual number of received bytes.
 * @param command Command whose reply is awaited.
 * @return 1 for a matching reply/control packet, 0 for an ACK or unrelated reply,
 *         -1 for a truncated or oversized packet.
 */
inline int harmony_z_tcp_packet(const std::uint8_t* packet, std::size_t size,
                                std::uint8_t command, bool acceptEmptyAck = false) {
    if (size < 4 || packet[0] < 3 || packet[0] > 60 || packet[0] >= size) return -1;
    if (packet[1] == TYPE_TCP_SYN || packet[1] == (TYPE_TCP_ACK | TYPE_TCP_FIN)) return 1;
    if (packet[1] == TYPE_TCP_ACK && packet[0] == 3) return acceptEmptyAck;
    if (packet[1] != TYPE_TCP_ACK || packet[0] < 5) return 0;
    if (packet[4] != TYPE_RESPONSE) return 0;
    return packet[5] == command ||
        (command == COMMAND_READ_REGION_DATA && packet[5] == COMMAND_READ_REGION_DONE);
}
