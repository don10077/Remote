/**
 * @file usbnet.cpp
 * @brief Кодирование и чтение пакетов протокола Harmony 900.
 */

#include "harmony/transport/usbnet.h"

namespace harmony::transport::usbnet {

namespace {
constexpr std::uint8_t kServiceClient = 2;
} // namespace

Result<std::uint8_t> encodeLength(std::size_t length) {
    if (length <= 0x3F) {
        return static_cast<std::uint8_t>(length);
    }
    if (length % 512 == 0 && length / 512 <= 0x3F) {
        return static_cast<std::uint8_t>(0xC0 | (length / 512));
    }
    if (length % 4 == 0 && length / 4 <= 0x3F) {
        return static_cast<std::uint8_t>(0x40 | (length / 4));
    }
    return makeError(ErrorCode::Format, "parameter length %1 cannot be encoded", {std::to_string(length)});
}

std::size_t decodeLength(std::uint8_t code) {
    const std::size_t value = code & 0x3FU;
    switch (code & 0xC0U) {
    case 0x40:
        return value * 4;
    case 0xC0:
        return value * 512;
    default:
        return value; // 0x00 и 0x80
    }
}

Result<std::vector<std::uint8_t>> encode(const Packet& packet) {
    const auto command = static_cast<std::uint16_t>(packet.command);
    std::vector<std::uint8_t> out = {
        static_cast<std::uint8_t>((kServiceClient << 4) | ((command >> 8) & 0x0F)),
        static_cast<std::uint8_t>(command & 0xFF),
        static_cast<std::uint8_t>(packet.request ? 0x80 : (packet.status & 0x7F)),
        static_cast<std::uint8_t>(packet.params.size()),
    };
    for (std::size_t i = 0; i < packet.params.size(); ++i) {
        const auto& param = packet.params[i];
        if (i < packet.lengthCodes.size()) {
            out.push_back(packet.lengthCodes[i]);
            out.insert(out.end(), param.begin(), param.end());
            continue;
        }
        auto length = encodeLength(param.size());
        if (!length) {
            return std::unexpected(std::move(length.error()));
        }
        out.push_back(*length);
        out.insert(out.end(), param.begin(), param.end());
    }
    return out;
}

Result<Packet> receive(Channel& channel, std::chrono::milliseconds timeout) {
    auto header = channel.receive(4, timeout);
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    Packet packet;
    packet.command = static_cast<Command>((((*header)[0] & 0x0F) << 8) | (*header)[1]);
    packet.request = ((*header)[2] & 0x80) != 0;
    packet.status = (*header)[2] & 0x7F;
    const std::size_t count = (*header)[3];
    for (std::size_t i = 0; i < count; ++i) {
        auto code = channel.receive(1, timeout);
        if (!code) {
            return std::unexpected(std::move(code.error()));
        }
        auto data = channel.receive(decodeLength((*code)[0]), timeout);
        if (!data) {
            return std::unexpected(std::move(data.error()));
        }
        packet.params.push_back(std::move(*data));
    }
    return packet;
}

std::vector<std::uint8_t> number(std::uint32_t value, std::size_t bytes) {
    std::vector<std::uint8_t> out(bytes);
    for (std::size_t i = 0; i < bytes; ++i) {
        out[bytes - 1 - i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    return out;
}

std::uint32_t toNumber(const std::vector<std::uint8_t>& param) {
    std::uint32_t value = 0;
    for (const std::uint8_t byte : param) {
        value = (value << 8) | byte;
    }
    return value;
}

} // namespace harmony::transport::usbnet
