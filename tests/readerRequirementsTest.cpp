/**
 * @file readerRequirementsTest.cpp
 * @brief I2: требования к каждому новому разборщику — семь решений, которые уже держит импорт
 *        бинарного HID.
 *
 * Это гейт для будущих строк `configReaders()`: новый формат обязан пройти те же проверки, что
 * и существующий `binary-hid`. Ожидания следуют из таблиц программы (семейства, словарь клавиш,
 * каталог) и из синтетической фикстуры (правило 19).
 *
 * На этапе А проверено: тесты 1–7 зелёные на существующем коде — импорт уже отвечает этим
 * требованиям, новых веток по моделям и форматам они не добавляют. Настоящие дампы в git не
 * входят, поэтому тест 6 пропускается без папки `dumps/`.
 */

#include "dumpFiles.h"
#include "formatFixtures.h"
#include "formats/binaryHidFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/container.h"
#include "harmony/core/importReport.h"
#include "harmony/core/keyScans.h"
#include "harmony/core/remoteFamily.h"
#include "harmony/core/writePolicy.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

using harmony::ConfigDocument;
using harmony::test::keyFixture;

/// Открытый документ синтетического бинарного импорта: arch 14, skin 72 — модель 650 каталога.
ConfigDocument binaryImport() {
    auto document = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 72), "requirements");
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return document ? std::move(*document) : ConfigDocument::createNew("requirements");
}

// 1. Различия семейства — поля `RemoteFamily`, а не ветки по архитектуре в разборщике.
TEST(ReaderRequirements, FamilySignsComeFromTheTablesNotFromModelBranches) {
    for (const auto& family : harmony::formats::containerFamilies()) {
        SCOPED_TRACE(family.cookie);
        const auto* remote = harmony::remoteFamily(family.architecture);
        ASSERT_NE(remote, nullptr) << "семейство контейнера без описания RemoteFamily";
        ASSERT_TRUE(remote->container.has_value());
        EXPECT_EQ(*remote->container, family.family);
    }

    // Признак файла и описание семейства дают одну и ту же архитектуру.
    const auto container = harmony::formats::parseContainer(
        harmony::formats::parseEzHex(keyFixture(14, 72, {0x2c}))->payload);
    ASSERT_TRUE(container) << container.error().message;
    ASSERT_NE(container->family, nullptr);
    const auto* remote = harmony::remoteFamily(container->family->architecture);
    ASSERT_NE(remote, nullptr);
    ASSERT_TRUE(remote->container.has_value());
    EXPECT_EQ(*remote->container, container->family->family);
}

// 2. Имена клавиш — словарь `keyNameForScan()`; неизвестный код — `ScanNN` и предупреждение.
TEST(ReaderRequirements, UnknownScanIsNamedAndReported) {
    EXPECT_EQ(harmony::keyNameForScan(9, "", 0x2c), std::nullopt);
    EXPECT_EQ(harmony::scanPlaceholder(0x2c), "Scan2C");

    const auto document = ConfigDocument::fromEzHex(keyFixture(9, 22, {0x2c}), "requirements");
    ASSERT_TRUE(document) << document.error().message;
    const auto& warnings = document->importReport().warnings;
    EXPECT_TRUE(std::ranges::any_of(warnings, [](const std::string& warning) {
        return warning.find("Scan2C") != std::string::npos;
    })) << "неизвестный код не попал в отчёт импорта";
}

// 3. Модель — по identity через каталог; свойство цели импорт не пишет.
TEST(ReaderRequirements, ModelComesFromIdentityThroughTheCatalog) {
    const auto document = binaryImport();
    EXPECT_EQ(document.targetRemote(), "650");
    EXPECT_TRUE(document.model().property("HarmonyTargetRemote").empty())
        << "импорт не выбирает модель за пользователя";

    const auto unknown = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 999), "requirements");
    ASSERT_TRUE(unknown) << unknown.error().message;
    EXPECT_TRUE(unknown->targetRemote().empty()) << "skin вне каталога не получает модель по умолчанию";
}

// 4. Возможности формата честно говорят, что импорт потерял.
TEST(ReaderRequirements, CapabilitiesTellWhatTheImportLost) {
    const auto document = binaryImport();
    ASSERT_TRUE(document.backend() != nullptr);
    EXPECT_EQ(document.backend()->format(), harmony::ConfigFormat::BinaryHid);

    const auto caps = document.backend()->capabilities();
    EXPECT_FALSE(caps.writeToRemote) << "бинарный импорт на пульт не пишется";
    EXPECT_FALSE(caps.saveToSource) << "правки не собираются обратно в исходный образ";
    EXPECT_TRUE(caps.importedApproximation);
    EXPECT_FALSE(caps.powerStateKnown);
    EXPECT_FALSE(document.backend()->canWriteToRemote());
}

// 5. Исходный образ остаётся с документом байт в байт.
TEST(ReaderRequirements, SourceImageStaysWithTheDocument) {
    const auto bytes = harmony::test::minimalEzHex(14, 72);
    const auto document = ConfigDocument::fromEzHex(bytes, "requirements");
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_TRUE(document->sourceImage().has_value()) << "у импорта нет исходного образа";
    EXPECT_TRUE(std::ranges::equal(*document->sourceImage(), bytes));
}

// 6. Настоящие байты — отдельный тест с пропуском; синтетика остаётся в git.
TEST(ReaderRequirements, RealDumpsAreOptionalAndSyntheticFixturesStandAlone) {
    struct Sample { const char* folder; const char* mask; };
    std::size_t checked = 0;
    for (const Sample sample : {Sample{"EZHex/harmony650", "Harmony_650.EZHex"},
                                Sample{"EZHex/harmony525", "config.EZHex"},
                                Sample{"EZHex/arch8", "H880-Bedroom.EZHex"},
                                Sample{"EZHex/harmony890", "H890-Bedroom-1.EZHex"}}) {
        SCOPED_TRACE(sample.folder);
        const auto path = harmony::test::dumpFile(sample.folder, sample.mask);
        if (path.empty()) continue;
        ++checked;
        const auto bytes = harmony::test::readFile(path);
        const auto document = ConfigDocument::fromEzHex(bytes, "dump");
        ASSERT_TRUE(document) << document.error().message;
        EXPECT_EQ(document->backend()->format(), harmony::ConfigFormat::BinaryHid);
        EXPECT_FALSE(document->backend()->canWriteToRemote());
        ASSERT_TRUE(document->sourceImage().has_value());
        EXPECT_TRUE(std::ranges::equal(*document->sourceImage(), bytes))
            << "исходный образ изменён при чтении дампа";
    }
    // Без папки дампов синтетическая фикстура всё равно проходит полный путь импорта.
    EXPECT_EQ(binaryImport().backend()->format(), harmony::ConfigFormat::BinaryHid);

    if (checked == 0) GTEST_SKIP() << "dumps/ отсутствует: проверка настоящих байтов пропущена";
}

// 7. Запись на пульт не включается ни форматом, ни правом модели.
TEST(ReaderRequirements, BinaryImportCannotBeWrittenToTheRemote) {
    const auto document = binaryImport();
    EXPECT_EQ(harmony::canWriteTo(document).verdict, harmony::WriteVerdict::WrongFormat);
    EXPECT_FALSE(document.toEzHex(true)) << "формат импорта не собирается для записи на пульт";
}

} // namespace
