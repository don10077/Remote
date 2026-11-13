// Фаззинг разбора .EZHex: заголовок, размер, контрольная сумма, затем ZIP внутри.
#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/zipArchive.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto ezhex = harmony::formats::parseEzHex(std::span<const std::uint8_t>(data, size));
    if (!ezhex) {
        return 0;
    }
    (void)harmony::formats::writeEzHex(*ezhex);
    (void)harmony::formats::readZip(ezhex->payload);
    return 0;
}
