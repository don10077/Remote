/**
 * @file ezhexTest.cpp
 * @brief Тесты .EZHex и ZIP: разбор настоящего дампа пульта, сборка байт в байт, замена файлов.
 */

#include "harmony/core/formats/ezhex.h"
#include "harmony/core/formats/zipArchive.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace harmony::formats;

namespace {

const std::filesystem::path kData(HARMONY_TEST_DATA_DIR);

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

Bytes sample() {
    return readFile(kData / "ezhex" / "harmony900.EZHex");
}

Bytes bytesOf(const std::string& text) {
    return {text.begin(), text.end()};
}

} // namespace

TEST(EzHex, ParsesDumpOfHarmony900) {
    auto ezhex = parseEzHex(sample());
    ASSERT_TRUE(ezhex) << ezhex.error().message;
    EXPECT_EQ(ezhex->identity.architecture, 15);
    EXPECT_EQ(ezhex->identity.skin, 61);
    EXPECT_EQ(ezhex->identity.flash, "0x01:0x49");
    EXPECT_EQ(ezhex->payload.size(), sample().size() - ezhex->header.size());
    EXPECT_TRUE(ezhex->header.ends_with("</INFORMATION>\r\n"));
}

TEST(EzHex, HeaderTemplateMatchesRealOne) {
    auto ezhex = parseEzHex(sample());
    ASSERT_TRUE(ezhex);

    EXPECT_EQ(makeEzHexHeader(ezhex->identity, ezhex->payload), ezhex->header);
}

TEST(EzHex, RejectsDamagedFiles) {
    Bytes file = sample();
    file.back() ^= 0x01;
    EXPECT_EQ(parseEzHex(file).error().code, harmony::ErrorCode::Format); // контрольная сумма
    file.pop_back();
    EXPECT_EQ(parseEzHex(file).error().code, harmony::ErrorCode::Format); // размер
    EXPECT_EQ(parseEzHex(bytesOf("PK\x03\x04")).error().code, harmony::ErrorCode::Format);
}

TEST(EzHex, WriteRefreshesSizeAndChecksum) {
    auto ezhex = parseEzHex(sample());
    ASSERT_TRUE(ezhex);
    auto written = writeEzHex(*ezhex);
    ASSERT_TRUE(written) << written.error().message;
    EXPECT_EQ(*written, sample());

    ezhex->payload.push_back(0x42);
    auto updated = writeEzHex(*ezhex);
    ASSERT_TRUE(updated) << updated.error().message;
    auto reread = parseEzHex(*updated);
    ASSERT_TRUE(reread) << reread.error().message;
    EXPECT_EQ(reread->payload, ezhex->payload);
}

TEST(EzHex, RejectsHeaderWithoutSizeOrChecksum) {
    EzHex ezhex;
    ezhex.payload = bytesOf("payload");
    ezhex.header = "<INFORMATION>\r\n    <BINARYDATASIZE>7</BINARYDATASIZE>\r\n</INFORMATION>\r\n";
    EXPECT_EQ(writeEzHex(ezhex).error().code, harmony::ErrorCode::Format); // нет <CHECKSUM>

    ezhex.header = "<INFORMATION>\r\n    <CHECKSUM>0</CHECKSUM>\r\n</INFORMATION>\r\n";
    EXPECT_EQ(writeEzHex(ezhex).error().code, harmony::ErrorCode::Format); // нет <BINARYDATASIZE>

    // Незакрытый тег раньше приводил к переполнению длины в std::string::replace.
    ezhex.header =
        "<INFORMATION>\r\n    <BINARYDATASIZE>7\r\n    <CHECKSUM>0</CHECKSUM>\r\n</INFORMATION>\r\n";
    EXPECT_EQ(writeEzHex(ezhex).error().code, harmony::ErrorCode::Format);
}

TEST(Zip, ReadsAllEntriesWithMetadata) {
    auto ezhex = parseEzHex(sample());
    auto zip = readZip(ezhex->payload);
    ASSERT_TRUE(zip) << zip.error().message;

    ASSERT_EQ(zip->entries.size(), 40U);
    EXPECT_EQ(zip->entries[0].name, "userconfig/");
    const ZipEntry* script = zip->find(".preinstall");
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->unixMode(), 0100775U);
    auto content = entryContent(*script);
    ASSERT_TRUE(content);
    EXPECT_TRUE(std::string(content->begin(), content->end()).starts_with("#!/bin/sh\n"));
    EXPECT_EQ(zip->find("userconfig/SsIr.bin")->method, 0); // мелочь хранится без сжатия
}

TEST(Zip, RoundTripIsLossless) {
    auto ezhex = parseEzHex(sample());
    auto zip = readZip(ezhex->payload);
    ASSERT_TRUE(zip);

    EXPECT_EQ(writeZip(*zip), ezhex->payload);
}

TEST(Zip, SameContentKeepsCompressedBytes) {
    auto zip = readZip(parseEzHex(sample())->payload);
    ZipEntry& entry = *zip->find("userconfig/ActionLists.xml");
    const Bytes before = entry.data;

    ASSERT_TRUE(setContent(entry, *entryContent(entry)));

    EXPECT_EQ(entry.data, before);
}

TEST(Zip, ReplacedContentReadsBack) {
    auto zip = readZip(parseEzHex(sample())->payload);
    const Bytes text = bytesOf("<?xml version=\"1.0\"?>\n<Root/>\n");
    ASSERT_TRUE(setContent(*zip->find("userconfig/ActionLists.xml"), text));

    auto reread = readZip(writeZip(*zip));
    ASSERT_TRUE(reread);
    EXPECT_EQ(*entryContent(*reread->find("userconfig/ActionLists.xml")), text);
    EXPECT_EQ(reread->find("userconfig/ActionLists.xml")->method, 8);
}

TEST(Zip, NewEntriesLookLikeRemoteOnes) {
    ZipArchive zip;
    const Bytes script = bytesOf("#!/bin/sh\necho ok\n");
    zip.entries.push_back(makeEntry("dir/", 040775, {}, 1'700'000'000));
    zip.entries.push_back(makeEntry("dir/run.sh", 0100775, script, 1'700'000'000));

    auto reread = readZip(writeZip(zip));
    ASSERT_TRUE(reread) << reread.error().message;
    EXPECT_EQ(reread->entries[0].unixMode(), 040775U);
    EXPECT_EQ(reread->entries[1].unixMode(), 0100775U);
    EXPECT_EQ(*entryContent(reread->entries[1]), script);
    EXPECT_EQ(reread->entries[1].localExtra.size(), 21U); // UT (время ×2) + Ux (root)
}

TEST(Zip, StoredEntryIsCheckedByCrc) {
    auto zip = readZip(parseEzHex(sample())->payload);
    ASSERT_TRUE(zip);
    ZipEntry& entry = *zip->find("userconfig/SsIr.bin");
    ASSERT_EQ(entry.method, 0);
    ASSERT_TRUE(entryContent(entry));

    entry.data.back() ^= 0x01;
    EXPECT_EQ(entryContent(entry).error().code, harmony::ErrorCode::Format);
}

TEST(Zip, RejectsEntryWithImplausibleSize) {
    // Заголовок обещает гигабайт, данных — несколько байт: распаковка не должна
    // выделять память по заявленному размеру (zip-bomb).
    const Bytes content(1000, 'a'); // сжимается, поэтому запись пойдёт методом deflate
    ZipEntry entry = makeEntry("userconfig/IrProto.bin", 0100775, content, 1'700'000'000);
    ASSERT_EQ(entry.method, 8);
    entry.size = 1024U * 1024U * 1024U;
    EXPECT_EQ(entryContent(entry).error().code, harmony::ErrorCode::Format);
}

TEST(Zip, RejectsTruncatedCentralDirectory) {
    const Bytes payload = parseEzHex(sample())->payload;
    ASSERT_TRUE(readZip(payload));
    // Комментария у архива нет: конец оглавления — последние 22 байта.
    const std::size_t end = payload.size() - 22;
    ASSERT_EQ(payload[end], 0x50);
    const std::size_t directory = payload[end + 16] | (payload[end + 17] << 8) |
                                  (payload[end + 18] << 16) | (static_cast<std::size_t>(payload[end + 19]) << 24);

    // Оглавление обрезано, конец оглавления цел (ссылается за пределы файла).
    Bytes cut(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(directory + 10));
    cut.insert(cut.end(), payload.begin() + static_cast<std::ptrdiff_t>(end), payload.end());
    EXPECT_EQ(readZip(cut).error().code, harmony::ErrorCode::Format);

    // Файл оборван: конца оглавления нет вовсе.
    const Bytes torn(payload.begin(), payload.end() - 30);
    EXPECT_EQ(readZip(torn).error().code, harmony::ErrorCode::Format);
    EXPECT_FALSE(readZip(Bytes(10, 0)));
}

TEST(Zip, ReadsArchiveWithComment) {
    auto zip = readZip(parseEzHex(sample())->payload);
    ASSERT_TRUE(zip);
    zip->comment = bytesOf("written by a test");

    auto reread = readZip(writeZip(*zip));
    ASSERT_TRUE(reread) << reread.error().message;
    EXPECT_EQ(reread->comment, zip->comment);
    ASSERT_EQ(reread->entries.size(), zip->entries.size());
    EXPECT_EQ(*entryContent(*reread->find("userconfig/ActionLists.xml")),
              *entryContent(*zip->find("userconfig/ActionLists.xml")));
}
