/**
 * @file noProfileMatrixTest.cpp
 * @brief T1: матрица «открыть без профиля». Ожидания — текущее поведение; где оно изменится
 *        запланированными шагами, помечено в тексте теста.
 *
 * Проверяются решения программы: какое имя модели документ выбирает, что запрещает, что
 * разрешает. Файл нужен только там, где решение зависит от настоящих байтов (identity
 * архитектуры 15 / skin 61); такие тесты читают `dumps/` через `dumpFiles.h` и пропускаются
 * без папки (правило 19). Тест «читается ли файл» не пишется.
 */

#include "dumpFiles.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/container.h"
#include "harmony/core/remoteProfile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using harmony::ConfigDocument;
using harmony::ErrorCode;
using harmony::formats::Bytes;

namespace {

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Документ с свойством HarmonyTargetRemote (XML в памяти, без файлов).
harmony::Result<ConfigDocument> documentWithTarget(const std::string& target) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties></Root>";
    return ConfigDocument::fromXml(xml, "matrix");
}

/**
 * Каталог текущей модели (`harmony::remoteProfiles`), который приложение строит из ресурсов.
 * Тест строит свой каталог и возвращает штатный в TearDown, чтобы не мешать другим тестам.
 */
class CatalogTest : public testing::Test {
protected:
    std::vector<harmony::RemoteProfile> saved;
    void SetUp() override { saved = harmony::remoteProfiles; }
    void TearDown() override { harmony::remoteProfiles = saved; }

    /// Каталог без профилей: модель не найдена.
    void clearCatalog() { harmony::remoteProfiles.clear(); }

    /// Модель в каталоге с указанным подключением; запись — по правилам ядра.
    void addToCatalog(const std::string& id, const std::string& driver, const std::string& format) {
        harmony::remoteProfiles.push_back({id, id, "photo.png",
                                          harmony::writableCombination(driver, format), driver, format});
    }
};

} // namespace

// Свойство важнее identity: документ назван тем, что в нём записано.
TEST_F(CatalogTest, PropertyIsTargetRemote) {
    const auto document = documentWithTarget("525");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_TRUE(harmony::remoteProfile("525") == nullptr) << "525 нет в штатном каталоге";
    EXPECT_EQ(document->targetRemote(), "525");
}

// Без свойства модель берётся из identity дампа (arch 15 / skin 61 → 900).
TEST_F(CatalogTest, IdentityGivesModelWhenNoProperty) {
    const auto dump = harmony::test::dumpFile("EZHex/harmony900", "*.EZHex");
    if (dump.empty()) GTEST_SKIP() << "dumps/EZHex/harmony900 нет — проверка только на машине владельца";
    const auto document = ConfigDocument::fromEzHex(readFile(dump), "без свойства");
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_TRUE(harmony::remoteProfile("900") != nullptr);
    EXPECT_EQ(document->targetRemote(), "900"); // M2: через каталог, результат тот же
}

// Ни свойства, ни identity — модель не выбрана.
TEST_F(CatalogTest, NoIdentityMeansUnknownModel) {
    const auto document = ConfigDocument::createNew("новая");
    EXPECT_EQ(document.targetRemote(), "unknown");
}

// 525 нельзя выбрать целевой моделью: её нет в каталоге.
TEST_F(CatalogTest, SetTargetRemoteRejectsModelWithoutProfile) {
    auto created = documentWithTarget("525");
    ASSERT_TRUE(created) << created.error().message;
    ConfigDocument document = std::move(*created);
    const auto result = document.setTargetRemote("525");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported) << result.error().message; // изменится в M3
    EXPECT_EQ(document.targetRemote(), "525") << "неудачная попытка ничего не изменила";
}

// Модель, которой нет в каталоге, не подключается по USB-сети. 1000i — модель каталога без
// профиля (visualProfile пустой), поэтому сегодня она не считается сетевой.
TEST_F(CatalogTest, UsesUsbNetFollowsCatalog) {
    clearCatalog();
    EXPECT_FALSE(harmony::usesUsbNet("1000i")) << "изменится в M4: каталог знает 1000i без профиля";
    addToCatalog("1000i", "usb-net", "device-sequence");
    EXPECT_TRUE(harmony::usesUsbNet("1000i"));
}

// Запись на пульт запрещена, когда каталог говорит «модель не пишется» (driver none),
// даже если документ — 900.
TEST_F(CatalogTest, WritingForbiddenWhenCatalogModelCannotWrite) {
    const auto dump = harmony::test::dumpFile("EZHex/harmony900", "*.EZHex");
    if (dump.empty()) GTEST_SKIP() << "dumps/EZHex/harmony900 нет — проверка только на машине владельца";
    const auto document = ConfigDocument::fromEzHex(readFile(dump), "driver none");
    ASSERT_TRUE(document) << document.error().message;
    clearCatalog();
    addToCatalog("900", "none", "harmony-xml");
    const auto result = document->toEzHex(true);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported) << result.error().message; // изменится в W3 (OD9)
}
