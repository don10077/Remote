/**
 * @file writePolicyGateTest.cpp
 * @brief W2: `toEzHex(true)` и интерфейс спрашивают `canWriteTo()`. Тест утверждает решение
 *        программы: какой документ можно отправить на пульт и с каким обоснованием отказ.
 *        Профиль оформления на решение не влияет (OD9). Ожидания — поведение после W2
 *        (правило 9, п. 2: этап А).
 */

#include "configFixtures.h"
#include "zipFixtures.h"

#include "harmony/core/configDocument.h"
#include "harmony/core/modelInfo.h"
#include "harmony/core/writePolicy.h"

#include <gtest/gtest.h>

#include <string>

using harmony::ConfigDocument;
using harmony::ErrorCode;

namespace {

/// XML-документ с целевой моделью; identity — по умолчанию (15/61), как у нового проекта.
harmony::Result<ConfigDocument> documentWithTarget(const std::string& target) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties></Root>";
    return ConfigDocument::fromXml(xml, "gate");
}

} // namespace

// Документ 900: решение `Allowed` — и `toEzHex(true)` его исполняет.
TEST(WritePolicyGate, AllowedDocumentIsWritten) {
    const ConfigDocument document = harmony::test::configFixture();
    ASSERT_EQ(harmony::canWriteTo(document).verdict, harmony::WriteVerdict::Allowed);

    const auto bytes = document.toEzHex(true);
    ASSERT_TRUE(bytes) << bytes.error().message;
    EXPECT_FALSE(bytes->empty());
}

// Отказ — тот же источник, что и решение: код ошибки Unsupported, текст равен reason из
// canWriteTo(). Проверка не дублирует правила в UI.
TEST(WritePolicyGate, RefusalTextIsTheDecisionReason) {
    const auto check = [](const char* name, const ConfigDocument& document, harmony::WriteVerdict verdict) {
        SCOPED_TRACE(name);
        const auto decision = harmony::canWriteTo(document);
        ASSERT_EQ(decision.verdict, verdict);
        EXPECT_FALSE(decision.reason.empty());

        const auto bytes = document.toEzHex(true);
        ASSERT_FALSE(bytes);
        EXPECT_EQ(bytes.error().code, ErrorCode::Unsupported);
        EXPECT_EQ(bytes.error().message, decision.reason) << "текст отказа — решение ядра";
    };

    // Формат, который на пульт не пишется.
    const auto binary = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 72), "650");
    ASSERT_TRUE(binary) << binary.error().message;
    check("650 binary import", *binary, harmony::WriteVerdict::WrongFormat);

    // Формат пишется, но на этой модели запись не подтверждена.
    const auto xml1100 = ConfigDocument::fromEzHex(harmony::test::zipEzHex(harmony::test::kConfigXml,
                                                                          {.architecture = 11, .skin = 63}), "1100");
    ASSERT_TRUE(xml1100) << xml1100.error().message;
    check("1100 xml", *xml1100, harmony::WriteVerdict::Experimental);

    // Skin документа не принадлежит выбранной модели.
    const auto mismatch = documentWithTarget("1100");
    ASSERT_TRUE(mismatch) << mismatch.error().message;
    check("900 file with target 1100", *mismatch, harmony::WriteVerdict::ModelMismatch);

    // Модель не выбрана.
    const auto unknown = ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 999), "без модели");
    ASSERT_TRUE(unknown) << unknown.error().message;
    check("no model", *unknown, harmony::WriteVerdict::UnknownModel);
}

// Диалог выбора модели решает без открытого документа: только каталог.
TEST(WritePolicyGate, ModelDecisionWithoutDocument) {
    EXPECT_EQ(harmony::canWriteTo(harmony::modelInfo("900")).verdict, harmony::WriteVerdict::Allowed);
    EXPECT_EQ(harmony::canWriteTo(harmony::modelInfo("525")).verdict, harmony::WriteVerdict::Experimental);
    EXPECT_EQ(harmony::canWriteTo(harmony::modelInfo("")).verdict, harmony::WriteVerdict::UnknownModel);
    EXPECT_EQ(harmony::canWriteTo(harmony::modelInfo("no-such")).verdict, harmony::WriteVerdict::UnknownModel);
}
