/**
 * @file gspm.cpp
 * @brief Проверка образа GSPM Harmony One и индекс команд секции 5.
 */

#include "harmony/core/formats/gspm.h"

#include "harmony/core/formats/container.h"

#include <algorithm>
#include <string>

namespace harmony::formats {

namespace {

constexpr std::size_t kMaxSize = 0x390000;       ///< Дальше лежит резервная копия приложения пульта
constexpr std::size_t kCommandSection = 5;       ///< Базовый слот индекса «устройство → команды»
constexpr std::size_t kRecordPrefix = 11;        ///< Указатель команды смотрит на байт +11 записи

std::uint32_t u16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8);
}

std::uint32_t u24(std::span<const std::uint8_t> data, std::size_t at) {
    return u16(data, at) | (static_cast<std::uint32_t>(data[at + 2]) << 16);
}

std::unexpected<Error> formatError(const char* text) {
    return makeError(ErrorCode::Format, text);
}

} // namespace

std::size_t GspmImage::sectionCount() const {
    return static_cast<std::size_t>(
        std::count_if(sections.begin(), sections.end(), [](const auto& s) { return s.has_value(); }));
}

std::size_t GspmCommandIndex::commandCount() const {
    std::size_t total = 0;
    for (const auto& device : devices) {
        total += device.size();
    }
    return total;
}

Result<GspmImage> parseGspm(std::span<const std::uint8_t> payload) {
    if (payload.size() >= kMaxSize) {
        return formatError("GSPM: the data is too large for the remote's configuration area");
    }
    // Рамку (признак, N, таблицу {spare, u24}, маркеры, XOR16) проверяет общий разбор.
    auto parsed = parseContainer(payload);
    if (!parsed) {
        return std::unexpected(std::move(parsed).error());
    }
    // Тот же признак `GSPM` у 600–700 с 20 слотами: One — это 22 слота, путать их нельзя.
    if (parsed->family->family != ContainerFamily::Arch12) {
        return makeError(ErrorCode::Format,
                         "GSPM: the container is architecture %1, not the Harmony One layout of 22 slots",
                         {std::to_string(parsed->family->architecture)});
    }

    GspmImage image;
    image.base = parsed->base;
    image.checksumOffset = parsed->checksumOffset;
    image.buttonCount = payload[parsed->family->markerOffset() + 4];
    for (std::size_t slot = 0; slot < kGspmRawSlotCount; ++slot) {
        image.sections[slot] = parsed->sections[slot];
    }

    // Пустые слоты — свойство архитектуры, а не конфигурации: 8 вставляет arch 12, а базовые
    // 18 и 19 (сырые 20 и 21) пусты в каждом прочитанном контейнере.
    for (const std::size_t slot : kGspmEmptySlots) {
        if (image.sections[slot]) {
            return makeError(ErrorCode::Format,
                             "GSPM: slot %1 points somewhere where architecture 12 leaves it empty",
                             {std::to_string(slot)});
        }
    }
    if (image.sectionCount() != kGspmRawSlotCount - kGspmEmptySlots.size()) {
        return makeError(ErrorCode::Format, "GSPM: the section table is not the 19-section layout of architecture 12");
    }
    return image;
}

std::optional<GspmRange> gspmSectionRange(const GspmImage& image, std::size_t index) {
    if (index >= kGspmBaseSlotCount) {
        return std::nullopt;
    }
    const auto& section = image.sections[gspmRawSlot(index)];
    if (!section) {
        return std::nullopt;
    }
    const std::size_t begin = *section;
    std::size_t end = image.checksumOffset;
    for (const auto& other : image.sections) {
        if (other && *other > begin && *other < end) {
            end = *other;
        }
    }
    return GspmRange{begin, end};
}

Result<GspmCommandIndex> readGspmCommands(std::span<const std::uint8_t> payload, const GspmImage& image) {
    const auto range = gspmSectionRange(image, kCommandSection);
    if (!range || range->end - range->begin < 1) {
        return formatError("GSPM: the command section is missing");
    }

    // Абсолютный указатель → смещение в образе; nullopt, если не помещается @p need байт.
    const auto resolve = [&](std::uint32_t absolute, std::size_t need) -> std::optional<std::size_t> {
        if (absolute < image.base) {
            return std::nullopt;
        }
        const std::size_t offset = absolute - image.base;
        if (offset > image.checksumOffset || need > image.checksumOffset - offset) {
            return std::nullopt;
        }
        return offset;
    };

    const std::size_t deviceCount = payload[range->begin];
    if (range->end - range->begin < 1 + 3 * deviceCount) {
        return formatError("GSPM: the device table is cut off");
    }

    GspmCommandIndex index;
    for (std::size_t device = 0; device < deviceCount; ++device) {
        const auto table = resolve(u24(payload, range->begin + 1 + 3 * device), 3);
        if (!table) {
            return formatError("GSPM: a device table points outside the image");
        }
        const std::size_t count = u16(payload, *table + 1);
        if (3 + 3 * count > image.checksumOffset - *table) {
            return formatError("GSPM: a command table is cut off");
        }

        auto& commands = index.devices.emplace_back();
        for (std::size_t command = 0; command < count; ++command) {
            const auto target = u24(payload, *table + 3 + 3 * command);
            // Указатель смотрит на байт +11 записи; режим лежит в +15, указатели сигналов в +16, +19, +22.
            if (target < image.base + kRecordPrefix) {
                return formatError("GSPM: a command record points outside the image");
            }
            const auto record = resolve(target - static_cast<std::uint32_t>(kRecordPrefix), 25);
            if (!record) {
                return formatError("GSPM: a command record points outside the image");
            }
            GspmCommand entry;
            entry.deviceOrdinal = static_cast<int>(device);
            entry.commandOrdinal = static_cast<int>(command);
            entry.record = *record;
            entry.toggles = payload[*record + 15] == 2;

            const auto press = resolve(u24(payload, *record + 16), 2);
            if (!press) {
                return formatError("GSPM: a press signal points outside the image");
            }
            entry.press = *press;
            if (const auto hold = u24(payload, *record + 19); hold != 0) {
                if (const auto at = resolve(hold, 2)) {
                    entry.hold = *at;
                }
            }
            if (const auto release = u24(payload, *record + 22); release != 0) {
                if (const auto at = resolve(release, 2)) {
                    entry.release = *at;
                }
            }
            commands.push_back(entry);
        }
    }
    return index;
}

} // namespace harmony::formats
