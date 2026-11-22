/**
 * @file noProfileMatrixTest.cpp
 * @brief T1: матрица «открыть без профиля». Проверяются решения программы: какое имя модели
 *        документ выбирает, что запрещает, что разрешает. Ожидания — текущее поведение; где
 *        запланированный шаг его изменит, помечено в тексте теста.
 *
 * Файл нужен только там, где решение зависит от настоящих байтов: такие тесты читают `dumps/`
 * через `dumpFiles.h` и пропускаются без папки (правило 19). Тест «читается ли файл» не пишется.
 */

#include "dumpFiles.h"
#include "formatFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteProfile.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

/// Документ с свойством HarmonyTargetRemote (XML в памяти, без файлов).
harmony::Result<ConfigDocument> documentWithTarget(const std::string& target) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties></Root>";
    return ConfigDocument::fromXml(xml, "matrix");
}

/**
 * Каталог модели (`harmony::remoteProfiles`), который приложение строит из ресурсов.
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

// Свойства нет — решение даёт identity. Сегодня identity без дампа по умолчанию архитектура 15
// и skin 61 (`ezhex.h:34–35`), поэтому программа называет такой документ 900. Это и есть
// ветвление по модели, запрещённое правилами: после M2 имя должно браться из каталога.
TEST_F(CatalogTest, IdentityWithoutPropertyNamesTheDefaultModel) {
    const auto document = ConfigDocument::createNew("новая");
    EXPECT_EQ(document.targetRemote(), "900") << "изменится в M2: имя берётся из каталога";
}

// Skin, модели которого нет в каталоге: программа не знает модель — ветвлений по skin нет.
// Фикстура собрана в коде, поэтому тест зелёный на любой машине.
TEST_F(CatalogTest, UnknownModelWhenSkinIsNotInTheCatalog) {
    const auto document = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 999), "без профиля");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(document->targetRemote(), "unknown");
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
// даже если документ выглядит как 900.
TEST_F(CatalogTest, WritingForbiddenWhenCatalogModelCannotWrite) {
    const auto document = ConfigDocument::createNew("driver none");
    clearCatalog();
    addToCatalog("900", "none", "harmony-xml");
    const auto result = document.toEzHex(true);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported) << result.error().message; // изменится в W3 (OD9)
}

// Настоящие байты: identity из дампа пульта 900 (архитектура 15, skin 61) без свойства называет
// документ 900. Без `dumps/` тест пропускается — это регрессия на машине владельца.
TEST_F(CatalogTest, IdentityFromRealDumpGivesModel) {
    const auto dump = harmony::test::dumpFile("EZHex/harmony900", "*.EZHex");
    if (dump.empty()) GTEST_SKIP() << "dumps/EZHex/harmony900 нет — только на машине владельца";
    const auto document = ConfigDocument::fromEzHex(harmony::test::readFile(dump), "без свойства");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_TRUE(harmony::remoteProfile("900") != nullptr);
    EXPECT_EQ(document->targetRemote(), "900"); // M2: через каталог, результат тот же
}
