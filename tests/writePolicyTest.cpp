/**
 * @file writePolicyTest.cpp
 * @brief W1: canWriteTo() — единственный источник права записи. Тесты утверждают решение
 *        программы по стабильным данным (каталог, фикстуры в git) — правило 19.
 *        Проверка данных: Allowed — ровно те документы, что сейчас проходят toEzHex(true).
 */

#include "configFixtures.h"
#include "zipFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/formats/hubConfig.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/writePolicy.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using harmony::ConfigDocument;

namespace {

/// XML-документ с целевой моделью; identity — по умолчанию (15/61), как у нового проекта.
harmony::Result<ConfigDocument> documentWithTarget(const std::string& target) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties></Root>";
    return ConfigDocument::fromXml(xml, "policy");
}

/// Конфигурация хаба — та же, что проверяет configHubDocumentTest.cpp.
constexpr std::string_view kHubJson = R"({
  "DeviceList.json": {"DevicesWithFeatures": [
    {"Device": {"Id-": 74521691, "Name": "Smart TV", "Manufacturer": "LG", "Model": "OLED65C4",
                "DeviceType": 1, "DeviceTypeDisplayName": "Television"},
     "Commands": [
       {"Name": "PowerOn", "KeyCode": "G:Toshiba 32 Bit:(0x20DF3AC5)(Repeat)():3", "TransportType": 1}
     ]}
  ]},
  "ActivityList.json": {"Activities": [
    {"Id-": 42111479, "Name": "Smart TV", "ActivityOrder": 1, "Roles": [
       {"DeviceId-": 74521691, "SelectedInput": {"Id-": 1, "Name": "HDMI 3"},
        "__type": "DisplayActivityRole:#Logitech.Harmony.Services"}]},
    {"Id-": -1, "Name": "PowerOff", "ActivityOrder": 0, "Roles": []}
  ]}
})";

} // namespace

TEST(WritePolicy, NinetyDocumentIsAllowed) {
    const ConfigDocument document = ConfigDocument::createNew("900");
    EXPECT_EQ(harmony::canWriteTo(document).verdict, harmony::WriteVerdict::Allowed);
}

TEST(WritePolicy, AllowedExactlyForWriteTestedModels) {
    // Проверка данных до подключения (W1): при текущих данных каталога и профилей
    // Allowed — ровно те документы, которые сейчас проходят toEzHex(true). Если Allowed
    // появился ещё у кого-то — остановиться и спросить владельца.
    std::vector<std::string> allowed;
    for (const auto& model : harmony::remoteCatalog()) {
        auto created = documentWithTarget(std::string(model.id));
        ASSERT_TRUE(created) << model.id << created.error().message;
        const bool gateNow = static_cast<bool>(created->toEzHex(true));
        const auto verdict = harmony::canWriteTo(*created).verdict;
        EXPECT_EQ(verdict == harmony::WriteVerdict::Allowed, gateNow) << model.id;
        if (verdict == harmony::WriteVerdict::Allowed) allowed.push_back(std::string(model.id));
    }
    EXPECT_EQ(allowed, (std::vector<std::string>{"900"}));
}

TEST(WritePolicy, XmlDocumentOfUntestedModelIsExperimental) {
    // Формат XML и identity 14/72 (650): модель известна, формат пишется на пульт,
    // skin совпадает — но запись на этой модели не подтверждена.
    const auto bytes = harmony::test::zipEzHex(harmony::test::kConfigXml, {.architecture = 14, .skin = 72});
    auto document = ConfigDocument::fromEzHex(bytes, "650 xml");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(harmony::canWriteTo(*document).verdict, harmony::WriteVerdict::Experimental);
}

TEST(WritePolicy, BinaryImportIsWrongFormat) {
    // Бинарный импорт (650): формат не пишется на пульт — независимо от модели.
    auto document = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 72), "650");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(harmony::canWriteTo(*document).verdict, harmony::WriteVerdict::WrongFormat);
}

TEST(WritePolicy, HubDocumentIsWrongFormat) {
    // Документ хаба: правится и сохраняется, но на пульт не пишется.
    auto hub = harmony::formats::parseHubConfig(kHubJson);
    ASSERT_TRUE(hub) << hub.error().message;
    auto document = ConfigDocument::fromHub(*hub, "hub");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(harmony::canWriteTo(*document).verdict, harmony::WriteVerdict::WrongFormat);
}

TEST(WritePolicy, SkinOutsideTheModelIsMismatch) {
    // Документ identity 15/61 с целью 1100: skin 61 не принадлежит 1100 (62–64).
    auto created = documentWithTarget("1100");
    ASSERT_TRUE(created) << created.error().message;
    EXPECT_EQ(harmony::canWriteTo(*created).verdict, harmony::WriteVerdict::ModelMismatch);
}

TEST(WritePolicy, UnknownModelIsReported) {
    // Skin 999 нет в каталоге — модель неизвестна; это правило раньше проверки формата.
    auto document = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 999), "без модели");
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(harmony::canWriteTo(*document).verdict, harmony::WriteVerdict::UnknownModel);
}
