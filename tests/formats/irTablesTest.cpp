/**
 * @file irTablesTest.cpp
 * @brief Тесты IrProto.bin и SsIr.bin: разбор, сборка байт в байт, перенос протоколов.
 *
 * Решения программы проверяются на данных, собранных в коде, — эти тесты зелёные на любой
 * машине. Настоящие байты проверяет отдельный тест через `dumpFiles.h`: без `dumps/` он
 * пропускается и остаётся регрессией на машине владельца (правило 19).
 */

#include "dumpFiles.h"
#include "formatFixtures.h"

#include "harmony/core/formats/irTables.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

using namespace harmony::formats;

namespace {

IrTable oneTable(std::uint8_t marker) {
    IrTable table;
    table.entries.push_back(harmony::test::protocolEntry(marker));
    return table;
}

} // namespace

TEST(IrTables, Crc32MatchesZlib) {
    const std::string text = "123456789";
    EXPECT_EQ(crc32({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}), 0xCBF43926U);
}

// Таблица, собранная в коде, читается обратно теми же записями: смещения и CRC сходятся.
TEST(IrTables, TableBuiltInCodeIsReadBackTheSame) {
    IrTable table;
    table.entries = {harmony::test::protocolEntry(0x11), harmony::test::protocolEntry(0x22),
                     harmony::test::protocolEntry(0x33)};

    const Bytes file = writeIrProto(table);
    const auto parsed = parseIrProto(file);
    ASSERT_TRUE(parsed) << parsed.error().message;
    ASSERT_EQ(parsed->entries.size(), table.entries.size());
    for (std::size_t i = 0; i < table.entries.size(); ++i) {
        EXPECT_EQ(parsed->entries[i], table.entries[i]) << "запись " << i;
    }
    // Хеш для XML берётся из обёртки, поэтому round trip его сохраняет.
    EXPECT_EQ(protocolHash(file), protocolHash(writeIrProto(*parsed)));
}

// Одинаковые протоколы не дублируются; новый — после существующих. Индекс следует из конструкции.
TEST(IrTables, InternKeepsOneCopyOfEachProtocol) {
    IrTable table;
    const Bytes first = harmony::test::protocolEntry(0x11);
    const Bytes second = harmony::test::protocolEntry(0x22);

    EXPECT_EQ(table.intern(first), 0U);
    EXPECT_EQ(table.intern(second), 1U);
    EXPECT_EQ(table.intern(first), 0U) << "тот же протокол получает тот же номер";
    EXPECT_EQ(table.entries.size(), 2U) << "дубль не добавляется";
    EXPECT_EQ(table.intern(harmony::test::protocolEntry(0x33)), 2U);
    EXPECT_EQ(table.entries.size(), 3U);
}

// Протокол из одной таблицы, перенесённый в другую, читается обратно тем же.
TEST(IrTables, RelocatedProtocolIsReadBackTheSame) {
    const IrTable source = oneTable(0x22);
    IrTable target;
    target.entries.push_back(harmony::test::protocolEntry(0x99));

    const Bytes moved = source.entries.front();
    const std::size_t index = target.intern(moved);
    EXPECT_EQ(index, 1U) << "перенесённый протокол — после существующей записи";

    const auto parsed = parseIrProto(writeIrProto(target));
    ASSERT_TRUE(parsed) << parsed.error().message;
    IrTable reread = *parsed;
    EXPECT_EQ(reread.entries[index], moved);
    EXPECT_EQ(reread.intern(moved), index);
}

// Две секции-указателя на одну секцию (как у Samsung32: повтор — тот же кадр).
TEST(IrTables, SharedSectionIsRelocatedOnce) {
    const Bytes header = {0x01, 0x4B, 0x66, 0x00, 0x00, 0x32, 0x02, 0x0B, 0x00, 0x0B, 0x00};
    Bytes entry(header.size() + 16, 0);
    std::copy(header.begin(), header.end(), entry.begin());
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

// Испорченная запись: контрольная сумма не сходится — файл отвергается.
TEST(IrTables, RejectsTableWithBrokenChecksum) {
    IrTable table;
    table.entries = {harmony::test::protocolEntry(0x11), harmony::test::protocolEntry(0x22)};
    Bytes file = writeIrProto(table);
    ASSERT_TRUE(parseIrProto(file));
    file[14] ^= 0xFF; // байт внутри записи, CRC не пересчитана
    EXPECT_EQ(parseIrProto(file).error().code, harmony::ErrorCode::Format);
}

// Указатель секции за пределами записи — таблицу записать нельзя.
TEST(IrTables, RejectsProtocolPointingOutsideItsEntry) {
    IrTable table = oneTable(0x11);
    table.entries[0][7] = 0xFF;   // указатель первой секции
    table.entries[0][8] = 0xFF;
    const auto built = buildIrProto(table);
    ASSERT_FALSE(built);
    EXPECT_EQ(built.error().code, harmony::ErrorCode::Format);
    EXPECT_TRUE(writeIrProto(table).empty());
}

// Смещения 16-битные: большая таблица не помещается, и писатель это отказывает.
TEST(IrTables, TableThatOverflowsSixteenBitOffsetsDoesNotFit) {
    IrTable table;
    EXPECT_TRUE(fitsOffsets(table));
    table.entries.push_back(Bytes(0xFFFF - 5, 0x01)); // 3 + 2 + 0xFFFA = 0xFFFF — ещё помещается
    EXPECT_TRUE(fitsOffsets(table));
    table.entries.back().push_back(0x01);
    EXPECT_FALSE(fitsOffsets(table));
    EXPECT_FALSE(buildIrProto(table));
}

TEST(IrTables, EmptyTablesAreValid) {
    const IrTable empty;
    auto proto = parseIrProto(writeIrProto(empty));
    auto raw = parseSsIr(writeSsIr(empty));
    ASSERT_TRUE(proto && raw);
    EXPECT_TRUE(proto->entries.empty());
    EXPECT_TRUE(raw->entries.empty());
}

// Регрессия на настоящих байтах: разобранный файл пульта собирается обратно байт в байт.
TEST(IrTables, RealDumpTablesAreWrittenBackByteForByte) {
    const auto dumps = harmony::test::dumpFiles("EZHex", "*.EZHex");
    if (dumps.empty()) GTEST_SKIP() << "dumps/ нет — только на машине владельца";

    std::size_t checked = 0;
    for (const auto& path : dumps) {
        const auto archive = harmony::test::archiveOf(path);
        if (archive.entries.empty()) continue; // binary HID без ZIP-архива
        for (const char* name : {"userconfig/IrProto.bin", "userconfig/SsIr.bin"}) {
            const Bytes file = harmony::test::entryData(archive, name);
            if (file.empty()) continue;
            const bool isProto = std::string(name) == "userconfig/IrProto.bin";
            const auto table = isProto ? parseIrProto(file) : parseSsIr(file);
            ASSERT_TRUE(table) << path.string() << " " << name << ": " << table.error().message;
            EXPECT_EQ(isProto ? writeIrProto(*table) : writeSsIr(*table), file) << path.string();
            ++checked;
        }
    }
    if (checked == 0) GTEST_SKIP() << "в dumps/ нет записей IrProto.bin/SsIr.bin";
}
