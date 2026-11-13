// Фаззинг ZIP: разбор, распаковка каждой записи, сборка обратно.
#include "harmony/core/formats/zipArchive.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto archive = harmony::formats::readZip(std::span<const std::uint8_t>(data, size));
    if (!archive) {
        return 0;
    }
    for (const auto& entry : archive->entries) {
        (void)harmony::formats::entryContent(entry);
    }
    const auto bytes = harmony::formats::writeZip(*archive);
    (void)harmony::formats::readZip(bytes);
    return 0;
}
