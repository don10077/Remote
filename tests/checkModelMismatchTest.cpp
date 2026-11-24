/**
 * @file checkModelMismatchTest.cpp
 * @brief C4: `ModelMismatch` из W1 — предупреждение в проверке, а не только при записи.
 *
 * Проверка использует то же решение, что `canWriteTo()` (правило 8), и называет обе величины:
 * skin файла и модель документа (правило 18). Красное — предупреждение в `check()`; зелёные
 * проверки — случаи, где предупреждения быть не может (skin принадлежит модели, модели нет).
 *
 * Ожидания следуют из данных каталога, заданных в коде (правило 19): skin фикстуры — 61,
 * это модель 900; модель 1100 — skins 62..64.
 */

#include "configFixtures.h"
#include "zipFixtures.h"

#include "harmony/core/configCheck.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/writePolicy.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr const char* kMismatch = "file skin %1 belongs to model %2, the document targets %3";

/// Документ XML (identity — skin 61) с целевой моделью @p target.
harmony::ConfigDocument documentWithTarget(const std::string& target) {
    const std::string xml = "<Root><Properties><Property name=\"HarmonyTargetRemote\">" + target +
                            "</Property></Properties></Root>";
    auto opened = harmony::ConfigDocument::fromXml(xml, "check");
    EXPECT_TRUE(opened) << opened.error().message;
    return std::move(*opened);
}

const harmony::Problem* problem(const std::vector<harmony::Problem>& problems,
                                const std::string& pattern) {
    const auto it = std::ranges::find_if(problems, [&](const harmony::Problem& p) {
        return p.what.format == pattern;
    });
    return it != problems.end() ? &*it : nullptr;
}

// Skin файла (61) не принадлежит модели 1100 → предупреждение, как при записи.
TEST(CheckModelMismatch, FileSkinOutsideTargetModelWarnsInCheck) {
    const auto document = documentWithTarget("1100");

    const auto problems = document.check();
    const auto* found = problem(problems, kMismatch);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->severity, harmony::Problem::Severity::Warning);
    EXPECT_EQ(found->where, harmony::Problem::Where::Config);
    EXPECT_EQ(found->fix, harmony::Problem::Fix::None);
    EXPECT_EQ(found->id, "1100");
    EXPECT_EQ(found->what.args, (std::vector<std::string>{"61", "900", "1100"}));
}

// Проверка и право записи решают по одному источнику: тот же verdict и та же пара значений.
TEST(CheckModelMismatch, CheckAndWritePolicyUseTheSameDecision) {
    const auto document = documentWithTarget("1100");
    const auto decision = harmony::canWriteTo(document);
    ASSERT_EQ(decision.verdict, harmony::WriteVerdict::ModelMismatch);

    const auto problems = document.check();
    const auto* found = problem(problems, kMismatch);
    ASSERT_NE(found, nullptr) << "проверка знает то же решение, что отказ при записи";
    EXPECT_TRUE(std::ranges::find(found->what.args, "1100") != found->what.args.end());

    // Когда решения нет, предупреждения нет ни в проверке, ни в отказе.
    const auto same = documentWithTarget("900");
    EXPECT_EQ(harmony::canWriteTo(same).verdict, harmony::WriteVerdict::Allowed);
    EXPECT_EQ(problem(same.check(), kMismatch), nullptr);
}

// Skin принадлежит целевой модели — предупреждения нет (факт, красным быть не может).
TEST(CheckModelMismatch, MatchingSkinHasNoWarning) {
    const auto document = documentWithTarget("900");
    EXPECT_TRUE(document.check().empty()) << "фикстура без содержимого: других проверок нет";
    EXPECT_EQ(problem(document.check(), kMismatch), nullptr);
}

// Файл архива со своим skin: модель отвечает каталог по skin файла, предупреждения нет.
TEST(CheckModelMismatch, ArchiveFileWithItsOwnSkinHasNoWarning) {
    const auto opened = harmony::ConfigDocument::fromEzHex(
        harmony::test::zipEzHex(harmony::test::kConfigXml, {.architecture = 14, .skin = 72}), "650");
    ASSERT_TRUE(opened) << opened.error().message;
    EXPECT_EQ(problem(opened->check(), kMismatch), nullptr);
}

// Проверка говорит о противоречии identity и модели независимо от права записи: формат
// бинарного импорта на пульт не пишется, но skin файла остаётся фактом (правило 18).
TEST(CheckModelMismatch, MismatchIsReportedWithoutWriteRight) {
    auto document = harmony::ConfigDocument::fromEzHex(harmony::test::minimalEzHex(14, 72), "650");
    ASSERT_TRUE(document) << document.error().message;
    const auto result = document->setTargetRemote("1100");
    ASSERT_TRUE(result) << result.error().message;

    EXPECT_EQ(harmony::canWriteTo(*document).verdict, harmony::WriteVerdict::WrongFormat);
    const auto problems = document->check();
    const auto* found = problem(problems, kMismatch);
    ASSERT_NE(found, nullptr) << "проверка знает противоречие, даже когда запись запрещена";
    EXPECT_EQ(found->what.args, (std::vector<std::string>{"72", "650", "1100"}));
}

// Документ без свойства цели: модель отвечает каталог по skin (факт M2/U2), предупреждения нет.
TEST(CheckModelMismatch, DocumentWithoutTargetPropertyHasNoWarning) {
    const auto document = documentWithTarget("");
    EXPECT_EQ(document.targetRemote(), "900") << "skin фикстуры — 61";
    EXPECT_EQ(problem(document.check(), kMismatch), nullptr);
}

} // namespace
