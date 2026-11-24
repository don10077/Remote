/**
 * @file configReadersTest.cpp
 * @brief I1: один вход «открыть конфигурацию из контейнера» — таблица читателей формата.
 *
 * Красное — решение о читателе: строки таблицы и признак из `containerFamilies()` (правило 8:
 * признаки не строками в коде). Зелёное — существующие пути открытия: они дают тот же результат,
 * что прежде (правило 19), включая текст ошибки для данных без признака.
 *
 * Ожидания следуют из конструкции фикстур: признак — тот, что записан в таблице семейств, ZIP —
 * тот, что собрал `writeZip()`.
 */

#include "configFixtures.h"
#include "formatFixtures.h"
#include "zipFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/container.h"
#include "harmony/core/formats/zipArchive.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using harmony::ConfigDocument;
using harmony::formats::Bytes;
using harmony::formats::EzHex;

const ConfigDocument::ConfigReader* reader(std::string_view name) {
    for (const auto& entry : ConfigDocument::configReaders()) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

/// Контейнер с данными @p payload; identity — 650 (arch 14, skin 72), модели в тесте нет.
EzHex container(Bytes payload) {
    EzHex file;
    file.identity.architecture = 14;
    file.identity.skin = 72;
    file.payload = std::move(payload);
    return file;
}

/// Один тест на каждое решение таблицы: имя, признак, открытие.
TEST(ConfigReaders, TableHasOneEntryPerFormat) {
    const auto readers = ConfigDocument::configReaders();
    ASSERT_EQ(readers.size(), 2U);
    // Порядок — приоритет: `fromEzHex()` берёт первого, который признал данные.
    EXPECT_EQ(readers[0].name, "binary-hid");
    EXPECT_EQ(readers[1].name, "user-xml");
    for (const auto& entry : readers) {
        EXPECT_TRUE(entry.accepts != nullptr);
        EXPECT_TRUE(entry.open != nullptr);
    }
}

// Признак бинарного образа — любое семейство из таблицы, а не список строк в коде.
TEST(ConfigReaders, BinaryReaderAcceptsEverySignFromFamilyTable) {
    const auto* binary = reader("binary-hid");
    const auto* xml = reader("user-xml");
    ASSERT_TRUE(binary != nullptr);
    ASSERT_TRUE(xml != nullptr);

    for (const auto& family : harmony::formats::containerFamilies()) {
        Bytes payload(family.cookie.begin(), family.cookie.end());
        payload.resize(64, 0);
        EXPECT_TRUE(binary->accepts(container(payload))) << family.cookie;
        EXPECT_FALSE(xml->accepts(container(payload))) << family.cookie;
    }

    Bytes unknown{'A', 'A', 'A', 'A'};
    unknown.resize(64, 0);
    EXPECT_FALSE(binary->accepts(container(unknown)));
}

// ZIP с деревом файлов пульта — путь читателя `user-xml`.
TEST(ConfigReaders, ZipPayloadBelongsToTheXmlReader) {
    const auto* binary = reader("binary-hid");
    const auto* xml = reader("user-xml");
    ASSERT_TRUE(binary != nullptr);
    ASSERT_TRUE(xml != nullptr);

    const auto parsed = harmony::formats::parseEzHex(harmony::test::zipEzHex(harmony::test::kConfigXml));
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_FALSE(binary->accepts(*parsed));
    EXPECT_TRUE(xml->accepts(*parsed));
}

// Число слотов — проверка самого читателя, не признак: файл с признаком бинарного образа и числом
// слотов, которого в таблице нет (у экстендера C-RE4A — 11 слотов, строка I6), всё равно достаётся
// читателю образа, а не читателю XML.
TEST(ConfigReaders, SlotCountIsNotPartOfTheSign) {
    const auto* binary = reader("binary-hid");
    ASSERT_TRUE(binary != nullptr);

    Bytes payload{'T', 'P', 'T', 'P', 255};   // N в 0x09: такого числа слотов в таблице нет
    payload.resize(64, 0);
    ASSERT_EQ(harmony::formats::findFamily("TPTP", 255), nullptr) << "число слотов не часть признака";
    EXPECT_TRUE(binary->accepts(container(payload)));
}

// Данные без признака остаются у последнего читателя (он же путь по умолчанию), поэтому текст
// ошибки для них не меняется — тот же, что у `readZip()`.
TEST(ConfigReaders, UnknownPayloadKeepsTheReaderError) {
    const auto* xml = reader("user-xml");
    ASSERT_TRUE(xml != nullptr);

    EzHex file;
    file.payload = Bytes{'J', 'u', 'n', 'k'};
    file.payload.resize(64, 0);
    const auto bytes = harmony::formats::writeEzHex(file);
    ASSERT_TRUE(bytes) << bytes.error().message;
    const auto parsed = harmony::formats::parseEzHex(*bytes);
    ASSERT_TRUE(parsed) << parsed.error().message;

    EXPECT_FALSE(reader("binary-hid")->accepts(*parsed));
    EXPECT_TRUE(xml->accepts(*parsed));

    const auto throughTable = xml->open(*parsed, *bytes, "test");
    const auto throughEntry = ConfigDocument::fromEzHex(*bytes, "test");
    ASSERT_FALSE(throughTable);
    ASSERT_FALSE(throughEntry);
    EXPECT_EQ(throughEntry.error().code, harmony::ErrorCode::Format);
    EXPECT_EQ(throughEntry.error().message, harmony::formats::readZip(parsed->payload).error().message);
    EXPECT_EQ(throughTable.error().message, throughEntry.error().message);
}

// Оба существующих пути открытия работают через таблицу и дают тот же результат.
TEST(ConfigReaders, TableOpensBothExistingPathsTheSameWayAsFromEzHex) {
    const auto* binary = reader("binary-hid");
    const auto* xml = reader("user-xml");
    ASSERT_TRUE(binary != nullptr);
    ASSERT_TRUE(xml != nullptr);

    const auto zipBytes = harmony::test::zipEzHex(harmony::test::kConfigXml);
    const auto binaryBytes = harmony::test::minimalEzHex(14, 72);

    const auto zip = harmony::formats::parseEzHex(zipBytes);
    const auto image = harmony::formats::parseEzHex(binaryBytes);
    ASSERT_TRUE(zip) << zip.error().message;
    ASSERT_TRUE(image) << image.error().message;

    const auto byTable = xml->open(*zip, zipBytes, "by table");
    const auto byEntry = ConfigDocument::fromEzHex(zipBytes, "by entry");
    ASSERT_TRUE(byTable) << byTable.error().message;
    ASSERT_TRUE(byEntry) << byEntry.error().message;
    EXPECT_EQ(byTable->backend()->format(), byEntry->backend()->format());
    EXPECT_EQ(byTable->model().devices.size(), byEntry->model().devices.size());

    const auto imageByTable = binary->open(*image, binaryBytes, "by table");
    const auto imageByEntry = ConfigDocument::fromEzHex(binaryBytes, "by entry");
    ASSERT_TRUE(imageByTable) << imageByTable.error().message;
    ASSERT_TRUE(imageByEntry) << imageByEntry.error().message;
    EXPECT_EQ(imageByTable->backend()->format(), harmony::ConfigFormat::BinaryHid);
    EXPECT_EQ(imageByTable->model().devices.size(), imageByEntry->model().devices.size());
    EXPECT_EQ(byEntry->backend()->format(), harmony::ConfigFormat::Harmony900);
}

} // namespace
