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

// Свойства нет — модель берётся из каталога по skin из identity. Без дампа identity по
// умолчанию — архитектура 15, skin 61 (`ezhex.h:34–35`), поэтому такой документ называется 900.
// Ветвления по модели в коде нет: skin → запись каталога.
TEST_F(CatalogTest, IdentityWithoutPropertyNamesModelFromCatalog) {
    const auto document = ConfigDocument::createNew("новая");
    EXPECT_EQ(document.targetRemote(), "900");
}

// Тот же путь для других семейств: имя даёт каталог, а не ветка «arch 11 → 1100».
TEST_F(CatalogTest, CatalogNamesModelsOfOtherFamilies) {
    const auto eleven = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(11, 63), "1100");
    ASSERT_TRUE(eleven) << eleven.error().message;
    EXPECT_EQ(eleven->targetRemote(), "1100");

    const auto fourteen = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 72), "650");
    ASSERT_TRUE(fourteen) << fourteen.error().message;
    EXPECT_EQ(fourteen->targetRemote(), "650");
}

// Skin, модели которого нет в каталоге: программа не знает модель и не подставляет
// «по умолчанию» (это будет выбор нового проекта, U2). Пустая строка — «модель неизвестна».
TEST_F(CatalogTest, UnknownModelWhenSkinIsNotInTheCatalog) {
    const auto document = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 999), "без профиля");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(document->targetRemote(), "");
}

// Q2: модель документа не зависит от того, что было выбрано до открытия, — её даёт сам
// документ. Проверяются пути «новый проект», «из XML» и «из EZHex»; выбор модели в окне
// на документ не пишется (свойство появляется только при явной смене, M5).
TEST_F(CatalogTest, DocumentModelComesFromDocumentNotFromPreviousSelection) {
    const auto created = ConfigDocument::createNew("новый");
    EXPECT_EQ(created.targetRemote(), "900");

    const auto xml = ConfigDocument::fromXml("<Root><Properties/></Root>", "xml");
    ASSERT_TRUE(xml) << xml.error().message;
    EXPECT_EQ(xml->targetRemote(), "900");

    const auto first = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 72), "первый");
    ASSERT_TRUE(first) << first.error().message;
    EXPECT_EQ(first->targetRemote(), "650");

    const auto second = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(11, 63), "второй");
    ASSERT_TRUE(second) << second.error().message;
    EXPECT_EQ(second->targetRemote(), "1100") << "открытие второго документа не зависит от первого";
}

// Модель каталога без профиля — допустимая цель: каталог и есть список моделей.
TEST_F(CatalogTest, SetTargetRemoteAcceptsCatalogModelWithoutProfile) {
    for (const std::string id : {"525", "885", "touch"}) {
        auto created = documentWithTarget("900");
        ASSERT_TRUE(created) << created.error().message;
        ConfigDocument document = std::move(*created);
        const auto result = document.setTargetRemote(id);
        ASSERT_TRUE(result) << id;
        EXPECT_EQ(document.targetRemote(), id);
        EXPECT_EQ(document.model().property("HarmonyTargetRemote"), id);
    }
}

// Пользовательская модель — папка remotes/<id>; её видно после reload() (реестр профилей).
// Без записи в реестре такая модель неизвестна — это ожидаемо.
TEST_F(CatalogTest, SetTargetRemoteAcceptsRegisteredUserProfile) {
    auto created = documentWithTarget("900");
    ASSERT_TRUE(created) << created.error().message;
    ConfigDocument document = std::move(*created);

    clearCatalog();
    EXPECT_FALSE(document.setTargetRemote("my-remote")) << "без reload() своя модель неизвестна";

    addToCatalog("my-remote", "none", "none"); // то, что делает reload() для папки remotes/my-remote
    const auto result = document.setTargetRemote("my-remote");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(document.targetRemote(), "my-remote");
}

// Пустой id — «без модели»: свойство удаляется, остаётся поведение модели без профиля.
TEST_F(CatalogTest, EmptyTargetRemoteRemovesProperty) {
    auto created = documentWithTarget("525");
    ASSERT_TRUE(created) << created.error().message;
    ConfigDocument document = std::move(*created);
    const auto result = document.setTargetRemote("");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(document.model().property("HarmonyTargetRemote"), "");
    EXPECT_EQ(document.targetRemote(), "900") << "identity без свойства даёт модель каталога";
}

// Неизвестный id — ошибка, документ не меняется.
TEST_F(CatalogTest, SetTargetRemoteRejectsUnknownId) {
    auto created = documentWithTarget("525");
    ASSERT_TRUE(created) << created.error().message;
    ConfigDocument document = std::move(*created);
    const auto result = document.setTargetRemote("no-such");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported) << result.error().message;
    EXPECT_EQ(document.targetRemote(), "525") << "неудачная попытка ничего не изменила";
}

// Связь — свойство модели каталога, профиль на неё не влияет. 1000 — USB-сеть без профиля.
TEST_F(CatalogTest, UsesUsbNetFollowsCatalog) {
    clearCatalog();
    EXPECT_TRUE(harmony::usesUsbNet("1000")) << "каталог знает 1000 без профиля";
    EXPECT_FALSE(harmony::usesUsbNet("650"));
    EXPECT_FALSE(harmony::usesUsbNet("no-such"));

    addToCatalog("my-remote", "usb-net", "harmony-xml");
    EXPECT_FALSE(harmony::usesUsbNet("my-remote")) << "профиль не влияет на связь";
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
