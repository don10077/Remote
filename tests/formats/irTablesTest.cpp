/**
 * @file irTablesTest.cpp
 * @brief Тесты IrProto.bin и SsIr.bin: разбор, сборка байт в байт, перенос протоколов.
 */

#include "harmony/core/formats/irTables.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

using namespace harmony::formats;

namespace {

const std::filesystem::path kData(HARMONY_TEST_DATA_DIR);

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(IrTables, Crc32MatchesZlib) {
    const std::string text = "123456789";
    EXPECT_EQ(crc32({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}), 0xCBF43926U);
}

TEST(IrTables, ParsesSampleIrProto) {
    const Bytes file = readFile(kData / "userconfig" / "IrProto.bin");
    auto table = parseIrProto(file);
    ASSERT_TRUE(table) << table.error().message;
    EXPECT_EQ(table->entries.size(), 5U); // в образце команды ссылаются на протоколы 0…4
    EXPECT_EQ(protocolHash(file), "0x8595F4FF"); // совпадает с хешем в UserConfiguration.xml
}

TEST(IrTables, IrProtoRoundTripIsLossless) {
    for (const char* name : {"userconfig/IrProto.bin", "irtables/IrProto8.bin"}) {
        const Bytes file = readFile(kData / name);
        auto table = parseIrProto(file);
        ASSERT_TRUE(table) << name << ": " << table.error().message;
        EXPECT_EQ(writeIrProto(*table), file) << name;
    }
}

TEST(IrTables, ProtocolsAreRelocatable) {
    // Протокол из одного файла, записанный в другой на другое место, читается обратно тем же.
    auto source = parseIrProto(readFile(kData / "irtables" / "IrProto8.bin"));
    auto target = parseIrProto(readFile(kData / "userconfig" / "IrProto.bin"));
    ASSERT_TRUE(source && target);
    const Bytes moved = source->entries.back();
    const std::size_t index = target->intern(moved);
    EXPECT_EQ(index, 5U);

    auto reread = parseIrProto(writeIrProto(*target));
    ASSERT_TRUE(reread) << reread.error().message;
    EXPECT_EQ(reread->entries[index], moved);
    EXPECT_EQ(reread->intern(moved), index); // одинаковые протоколы не дублируются
}

TEST(IrTables, SharedSectionIsRelocatedOnce) {
    // Две секции-указателя на одну секцию (как у Samsung32: повтор — тот же кадр).
    Bytes entry = {0x01, 0x4B, 0x66, 0x00, 0x00, 0x32, 0x02, 0x0B, 0x00, 0x0B, 0x00};
    entry.resize(11 + 16, 0);
    entry[11 + 12] = static_cast<std::uint8_t>(entry.size()); // начало кадра — сразу за секцией
    entry.push_back(0x01);
    entry.push_back(0x94);
    entry.push_back(0x91);
    IrTable table;
    table.entries.push_back(Bytes(9, 0x01)); // сдвигает запись, чтобы смещение было не нулевым
    table.entries.front()[0] = 0x01;
    table.entries.front()[6] = 0x00;
    table.entries.push_back(entry);
    const Bytes file = writeIrProto(table);
    // Указатель «начало кадра» в файле — абсолютный и указывает на 01 94 91.
    const std::size_t base = 3 + 2 * 2 + 9;
    const std::size_t pointer = file[8 + 5 + base + 11 + 12] | (file[8 + 5 + base + 11 + 13] << 8);
    EXPECT_EQ(pointer, base + 27);
    auto reread = parseIrProto(file);
    ASSERT_TRUE(reread) << reread.error().message;
    EXPECT_EQ(reread->entries[1], entry);
}

TEST(IrTables, RejectsDamagedIrProto) {
    Bytes file = readFile(kData / "userconfig" / "IrProto.bin");
    file[40] ^= 0xFF;
    EXPECT_EQ(parseIrProto(file).error().code, harmony::ErrorCode::Format);
}

TEST(IrTables, SsIrRoundTripIsLossless) {
    for (const char* name : {"userconfig/SsIr.bin", "irtables/SsIr2.bin"}) {
        const Bytes file = readFile(kData / name);
        auto table = parseSsIr(file);
        ASSERT_TRUE(table) << name << ": " << table.error().message;
        EXPECT_EQ(writeSsIr(*table), file) << name;
    }
    EXPECT_EQ(parseSsIr(readFile(kData / "irtables" / "SsIr2.bin"))->entries.size(), 2U);
}

TEST(IrTables, TableThatOverflowsSixteenBitOffsetsDoesNotFit) {
    IrTable table;
    EXPECT_TRUE(fitsOffsets(table));
    table.entries.push_back(Bytes(0xFFFF - 5, 0x01)); // 3 + 2 + 0xFFFA = 0xFFFF — ещё помещается
    EXPECT_TRUE(fitsOffsets(table));
    table.entries.back().push_back(0x01);
    EXPECT_FALSE(fitsOffsets(table));
}

TEST(IrTables, EmptyTablesAreValid) {
    const IrTable empty;
    auto proto = parseIrProto(writeIrProto(empty));
    auto raw = parseSsIr(writeSsIr(empty));
    ASSERT_TRUE(proto && raw);
    EXPECT_TRUE(proto->entries.empty());
    EXPECT_TRUE(raw->entries.empty());
}

TEST(IrTables, RejectsProtocolPointingOutsideItsEntry) {
    // Указатель первой секции первого протокола — до начала записи; CRC пересчитан,
    // поэтому отказ — из-за самого протокола.
    Bytes file = readFile(kData / "userconfig" / "IrProto.bin");
    ASSERT_TRUE(parseIrProto(file));
    constexpr std::size_t kData0 = 8 + 5; // обёртка и заголовок; дальше 01, число записей, смещения
    const std::size_t first = kData0 + (file[kData0 + 3] | (file[kData0 + 4] << 8));
    ASSERT_GT(file[first + 6], 0U); // у протокола есть секции
    file[first + 7] = 0x01;
    file[first + 8] = 0x00;
    const std::uint32_t crc = crc32(std::span<const std::uint8_t>(file).subspan(8));
    file[0] = static_cast<std::uint8_t>(crc & 0xFF);
    file[1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    file[2] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
    file[3] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
    EXPECT_EQ(parseIrProto(file).error().code, harmony::ErrorCode::Format);
}

TEST(IrTables, DamagedProtocolIsNotWritten) {
    auto table = parseIrProto(readFile(kData / "userconfig" / "IrProto.bin"));
    ASSERT_TRUE(table) << table.error().message;
    ASSERT_TRUE(buildIrProto(*table));
    // Указатель секции за пределами записи: переложить запись в таблицу нельзя.
    table->entries[0][7] = 0xFF;
    table->entries[0][8] = 0xFF;
    const auto built = buildIrProto(*table);
    ASSERT_FALSE(built);
    EXPECT_EQ(built.error().code, harmony::ErrorCode::Format);
    EXPECT_TRUE(writeIrProto(*table).empty());

    IrTable huge;
    huge.entries.push_back(Bytes(0x10000, 0x01));
    EXPECT_FALSE(buildIrProto(huge));
}
