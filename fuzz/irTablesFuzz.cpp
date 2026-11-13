// Фаззинг таблиц ИК: одни и те же байты разбираются и как IrProto.bin, и как SsIr.bin.
#include "harmony/core/formats/irTables.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace harmony::formats;
    const std::span<const std::uint8_t> file(data, size);

    if (const auto proto = parseIrProto(file); proto && fitsOffsets(*proto)) {
        const auto bytes = writeIrProto(*proto);
        (void)protocolHash(bytes);
        (void)parseIrProto(bytes);
    }
    if (const auto raw = parseSsIr(file); raw && fitsOffsets(*raw)) {
        (void)parseSsIr(writeSsIr(*raw));
    }
    return 0;
}
