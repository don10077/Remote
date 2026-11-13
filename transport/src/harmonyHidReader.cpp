// SPDX-License-Identifier: GPL-3.0-only
// Протокол изучен по concordance/libconcord/remote.cpp и remote_info.h
// (Kevin Timmerman, Phil Dibowitz; GPL-3.0-or-later). См. docs/hid-reading.md.
#include "harmony/transport/harmonyHidReader.h"

#include <algorithm>
#include <cstdio>

namespace harmony::transport {
namespace {
/// Проверяет пару architecture/skin, прежде чем выбирать карту памяти.
std::string modelName(int architecture, int skin) {
    if (architecture == 12 && skin == 54) return "Harmony One";
    if (architecture != 14) return {};
    switch (skin) {
    case 66: case 69: return "Harmony 700";
    case 71: case 73: return "Harmony 600";
    case 72: case 74: return "Harmony 650";
    case 75: return "Harmony 665";
    default: return {};
    }
}

/// Ёмкость известных микросхем в байтах; неизвестные не угадываем.
std::uint32_t flashSize(std::uint8_t manufacturer, std::uint8_t id) {
    const unsigned key = (static_cast<unsigned>(manufacturer) << 8) | id;
    switch (key) {
    case 0x0137: return 1024 * 1024;
    case 0x0149: case 0x014c: return 2048 * 1024;
    case 0x151c: case 0x1fc8: return 4096 * 1024;
    case 0xff11: return 256 * 1024;
    case 0xff12: return 512 * 1024;
    default: return 0;
    }
}
}

Result<std::vector<std::uint8_t>> HarmonyHidReader::readFlash(
    std::uint32_t address, std::size_t size, const ProgressHandler& progress) {
    if (size == 0 || address > 0xffffff || size > 0x1000000 - address)
        return makeError(ErrorCode::Format, "Invalid HID flash range");
    std::vector<std::uint8_t> data;
    data.reserve(size);
    constexpr std::array<std::size_t, 16> lengths{0, 0, 1, 2, 3, 4, 5, 6, 14, 30, 62, 0, 0, 0, 0, 0};
    while (data.size() < size) {
        if (progress && !progress(Stage::Read, data.size(), size))
            return makeError(ErrorCode::Cancelled, "Reading cancelled");
        const auto chunk = std::min(size - data.size(), legacyProtocol_ ? std::size_t{700} : std::size_t{1022});
        const auto start = data.size();
        const auto pos = address + static_cast<std::uint32_t>(start);
        std::array<std::uint8_t, 64> request{};
        request[0] = 0x55;
        request[1] = static_cast<std::uint8_t>(pos >> 16);
        request[2] = static_cast<std::uint8_t>(pos >> 8);
        request[3] = static_cast<std::uint8_t>(pos);
        request[4] = static_cast<std::uint8_t>(chunk >> 8);
        request[5] = static_cast<std::uint8_t>(chunk);
        if (auto sent = channel_.send(request); !sent) return std::unexpected(sent.error());
        std::uint8_t sequence = 1;
        for (;;) {
            if (progress && !progress(Stage::Read, data.size(), size))
                return makeError(ErrorCode::Cancelled, "Reading cancelled");
            auto reply = channel_.receive();
            if (!reply) return std::unexpected(reply.error());
            if (reply->size() < 2) return makeError(ErrorCode::Remote, "Short HID response");
            const auto kind = (*reply)[0] & 0xf0;
            if (kind == 0xf0) {
                if (data.size() - start != chunk)
                    return makeError(ErrorCode::Remote, "Incomplete HID flash response");
                break;
            }
            const auto code = (*reply)[0] & 0x0f;
            const auto count = legacyProtocol_ ? (code >= 2 ? static_cast<std::size_t>(code - 1) : 0) : lengths[code];
            if (kind != 0x60 || (*reply)[1] != sequence || count == 0 ||
                count + 2 > reply->size() || count > chunk - (data.size() - start))
                return makeError(ErrorCode::Remote, "Invalid HID flash packet or sequence");
            data.insert(data.end(), reply->begin() + 2, reply->begin() + 2 + static_cast<std::ptrdiff_t>(count));
            sequence = static_cast<std::uint8_t>(sequence + 0x11);
        }
    }
    if (progress && !progress(Stage::Read, size, size))
        return makeError(ErrorCode::Cancelled, "Reading cancelled");
    return data;
}

Result<HidBackup> HarmonyHidReader::read(const ProgressHandler& progress) {
    if (progress && !progress(Stage::Connect, 0, 0))
        return makeError(ErrorCode::Cancelled, "Reading cancelled");
    std::array<std::uint8_t, 64> request{};
    request[0] = 0x10;
    if (auto sent = channel_.send(request); !sent) return std::unexpected(sent.error());
    auto report = channel_.receive();
    if (!report) return std::unexpected(report.error());
    if (report->size() < 8 || (((*report)[0] != 0x27) && ((*report)[0] != 0x28)))
        return makeError(ErrorCode::Remote, "Unexpected HID identity response");
    const auto& bytes = *report;
    HidBackup backup;
    backup.identityReport = bytes;
    auto& info = backup.info;
    info.identity.architecture = bytes[5] >> 4;
    info.identity.skin = bytes[6];
    info.identity.softwareType = bytes[5] & 0x0f;
    info.model = modelName(info.identity.architecture, info.identity.skin);
    if (info.model.empty())
        return makeError(ErrorCode::Unsupported, "Unsupported HID remote: architecture %1, skin %2",
                         {std::to_string(info.identity.architecture), std::to_string(info.identity.skin)});
    legacyProtocol_ = bytes[0] == 0x27 && bytes[7] == 0;
    info.firmwareVersion = std::to_string(bytes[1] >> 4) + "." + std::to_string(bytes[1] & 15);
    info.identity.board = std::to_string(bytes[2] >> 4) + "." + std::to_string(bytes[2] & 15) + ".0";
    char flash[16]{};
    std::snprintf(flash, sizeof(flash), "0x%02X:0x%02X", static_cast<unsigned>(bytes[4]), static_cast<unsigned>(bytes[3]));
    info.identity.flash = flash;
    const std::uint32_t configBase = info.identity.architecture == 12 ? 0x040000 : 0x030000;
    backup.configBase = configBase;
    const auto capacity = flashSize(bytes[4], bytes[3]);
    if (capacity <= configBase)
        return makeError(ErrorCode::Unsupported, "Unknown HID flash chip: %1", {info.identity.flash});
    auto prefix = readFlash(configBase, 1024, progress);
    if (!prefix) return std::unexpected(prefix.error());
    if ((*prefix)[0] != 0x47 || (*prefix)[1] != 0x53 || (*prefix)[2] != 0x50 || (*prefix)[3] != 0x4d)
        return makeError(ErrorCode::Format, "HID configuration signature is invalid");
    const auto end = static_cast<std::uint32_t>((*prefix)[4]) |
                     (static_cast<std::uint32_t>((*prefix)[5]) << 8) |
                     (static_cast<std::uint32_t>((*prefix)[6]) << 16);
    if (end < configBase + 4 || end > capacity - 4)
        return makeError(ErrorCode::Format, "HID configuration size is invalid");
    info.configSize = end - configBase + 4;
    auto config = readFlash(configBase, info.configSize, progress);
    if (!config) return std::unexpected(config.error());
    // Смена конфигурации во время чтения/повреждённый заголовок не должны дать успешный дамп.
    if (!std::equal(prefix->begin(), prefix->begin() + static_cast<std::ptrdiff_t>(std::min(prefix->size(), config->size())), config->begin()))
        return makeError(ErrorCode::Remote, "HID configuration changed while reading");
    backup.config = std::move(*config);
    return backup;
}
} // namespace harmony::transport
