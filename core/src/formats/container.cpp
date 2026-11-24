/**
 * @file container.cpp
 * @brief Разбор общей рамки контейнера конфигураций (arch 8, 9, 10, 12, 14, 16).
 *
 * Описание формата — reference/config-format/container.md проекта
 * harmony-explorations (MIT), свёрено с support/remotes-plan.md.
 */

#include "harmony/core/formats/container.h"

#include <algorithm>
#include <cstring>

namespace harmony::formats {

namespace {

constexpr std::size_t kAddressOf = 1; ///< В элементе таблицы адрес идёт после байта-заполнителя

std::uint16_t u16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at]) | (static_cast<std::uint16_t>(data[at + 1]) << 8);
}

std::uint32_t u24(std::span<const std::uint8_t> data, std::size_t at) {
    return u16(data, at) | (static_cast<std::uint32_t>(data[at + 2]) << 16);
}

std::uint32_t u32(std::span<const std::uint8_t> data, std::size_t at) {
    return u24(data, at) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

bool markerAt(std::span<const std::uint8_t> data, std::size_t at, std::string_view marker) {
    return std::memcmp(data.data() + at, marker.data(), marker.size()) == 0;
}

std::unexpected<Error> formatError(std::string text) {
    return makeError(ErrorCode::Format, std::move(text));
}

} // namespace

const std::array<ContainerFamilyInfo, 7>& containerFamilies() {
    // Признак и число слотов вместе дают архитектуру однозначно: у arch 8 и 10
    // один признак TPTP, но 21 и 23 слота; у GSPM — 22 (One), 20 (600–700) и 15 (300–350).
    static constexpr std::array<ContainerFamilyInfo, 7> kFamilies{{
        {ContainerFamily::Arch8, 8, "TPTP", "WLWL", "DKDK", 21},
        {ContainerFamily::Arch9, 9, "AHCM", "CMAH", "MCHA", 20},
        // Заполнение после концевого маркера — факт образов arch 10 (его снимает и пишет пульт).
        {ContainerFamily::Arch10, 10, "TPTP", "WLWL", "DKDK", 23, true, true},
        {ContainerFamily::Arch12, 12, "GSPM", "LWJL", "PTYY", 22},
        {ContainerFamily::Arch14, 14, "GSPM", "LWJL", "PTYY", 20},
        {ContainerFamily::Arch16, 16, "GSPM", "LWJL", "PTYY", 15},
        // Экстендер C-RE4A: рамка arch 10, но 11 слотов. Семантика секций не подтверждена
        // данными — 11 слотов не укладываются в карту слотов arch 10, где номера доходят до 22.
        {ContainerFamily::Arch10, 10, "TPTP", "WLWL", "DKDK", 11, false, false},
    }};
    return kFamilies;
}

const ContainerFamilyInfo* findFamily(std::string_view cookie, std::uint32_t slots) {
    const auto& families = containerFamilies();
    const auto found = std::ranges::find_if(families, [&](const ContainerFamilyInfo& info) {
        return info.cookie == cookie && info.slots == slots;
    });
    return found == families.end() ? nullptr : &*found;
}

std::size_t Container::sectionCount() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(sections, [](const auto& section) { return section.has_value(); }));
}

Result<Container> parseContainer(std::span<const std::uint8_t> payload) {
    if (payload.size() < kContainerTableOffset + 4) {
        return formatError("container: the data is shorter than the fixed header");
    }
    // Слово в 0x08 — это N в байте 0x09 (старшие разряды нулевые), а не версия.
    const std::string_view cookie(reinterpret_cast<const char*>(payload.data()), 4);
    const std::uint32_t slots = payload[9];
    const ContainerFamilyInfo* family = findFamily(cookie, slots);
    if (!family) {
        return formatError("container: unknown signature and slot count (cookie '" + std::string(cookie)
                           + "', N = " + std::to_string(slots) + ")");
    }

    const std::size_t marker = family->markerOffset();
    if (payload.size() < marker + family->marker.size() + 6) {
        return formatError(std::string(family->cookie) + ": the data is shorter than "
                           + std::to_string(family->slots) + " section slots plus the trailer");
    }
    // Конец образа — смещение концевого маркера. Заполнение нулями после него признаёт только строка
    // таблицы, где `trailingPaddingAllowed`; для остальных строк хвост — часть данных.
    std::size_t end = payload.size();
    if (family->trailingPaddingAllowed) {
        while (end > 0 && payload[end - 1] == 0) {
            --end;
        }
    }
    if (end < family->endMarker.size()) {
        return formatError(std::string(family->cookie) + ": the data is shorter than the end marker");
    }
    end -= family->endMarker.size();
    if (!markerAt(payload, end, family->endMarker)) {
        return formatError(std::string(family->cookie) + ": the end marker " + std::string(family->endMarker)
                           + " is missing at the end of the data");
    }
    if (!markerAt(payload, marker, family->marker)) {
        return formatError(std::string(family->cookie) + ": the marker " + std::string(family->marker)
                           + " is not at 0x0B + 4N as " + std::to_string(family->slots) + " slots would mean");
    }
    // N из байта 0x09 и маркер — два независимых пути к числу слотов: раньше маркера
    // (четырёх заглавных букв после трёх нулей) в таблице быть не должно.
    for (std::size_t at = kContainerTableOffset + 1; at < marker; ++at) {
        if (markerAt(payload, at, family->marker) && payload[at - 3] == 0 && payload[at - 2] == 0
            && payload[at - 1] == 0) {
            return formatError(std::string(family->cookie)
                               + ": slot count from 0x09 disagrees with the marker position in the table");
        }
    }

    Container container;
    container.family = family;
    container.endOffset = end;
    container.checksumOffset = end - 2;
    container.checksumStored = u16(payload, container.checksumOffset);
    container.endAddress = u32(payload, 4);
    if (container.endAddress <= end) {
        return formatError(std::string(family->cookie)
                           + ": the end pointer is below the size of the data");
    }
    container.base = container.endAddress - static_cast<std::uint32_t>(end);

    container.sections.reserve(family->slots);
    std::uint32_t previous = 0;
    for (std::uint32_t slot = 0; slot < family->slots; ++slot) {
        const std::size_t item = kContainerTableOffset + 4 * slot;
        // Элемент — {u8 spare; u24 address}: читать его как u32 нельзя.
        if (payload[item] != 0) {
            return formatError(std::string(family->cookie) + ": slot " + std::to_string(slot)
                               + " has a nonzero spare byte before its pointer");
        }
        const std::uint32_t absolute = u24(payload, item + kAddressOf);
        if (absolute == 0) {
            container.sections.emplace_back();
            continue;
        }
        // Последний слот пуст во всех семействах: на его нули опирается маркер за таблицей.
        if (slot + 1 == family->slots) {
            return formatError(std::string(family->cookie)
                               + ": the last slot points somewhere where the marker follows the table");
        }
        if (absolute < container.base || absolute - container.base >= container.checksumOffset) {
            return formatError(std::string(family->cookie) + ": slot " + std::to_string(slot)
                               + " points outside the container");
        }
        // Непустые указатели растут вместе с номером слота: секции разложены по порядку.
        if (previous != 0 && absolute <= previous) {
            return formatError(std::string(family->cookie) + ": slot " + std::to_string(slot)
                               + " does not ascend over the previous section pointer");
        }
        previous = absolute;
        container.sections.emplace_back(static_cast<std::size_t>(absolute - container.base));
    }

    // Сумма сходится, а не предполагается: remote отвергает контейнер с несовпавшей суммой.
    if (containerChecksum(payload, container.checksumOffset) != container.checksumStored) {
        return formatError(std::string(family->cookie) + ": checksum mismatch, the container is damaged");
    }
    return container;
}

std::optional<ContainerRange> containerSectionRange(const Container& container, std::size_t index) {
    if (index >= container.sections.size() || !container.sections[index]) {
        return std::nullopt;
    }
    const std::size_t begin = *container.sections[index];
    std::size_t end = container.checksumOffset;
    for (const auto& other : container.sections) {
        if (other && *other > begin && *other < end) {
            end = *other;
        }
    }
    return ContainerRange{begin, end};
}

std::uint16_t containerChecksum(std::span<const std::uint8_t> payload, std::size_t checksumOffset) {
    std::uint16_t accumulator = kContainerChecksumSeed;
    for (std::size_t i = 0; i + 2 <= checksumOffset; i += 2) {
        accumulator ^= u16(payload, i);
    }
    return accumulator;
}

} // namespace harmony::formats
