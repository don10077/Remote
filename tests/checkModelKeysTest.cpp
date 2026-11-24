/**
 * @file checkModelKeysTest.cpp
 * @brief C1: проверка «клавиши нет на этой модели» (Q5).
 *
 * Основа — фикстура T2 `tests/data/fixtures/xml-unknown-target.xml` (назначения Play,
 * SoftRight4, Number5): ожидания следуют из данных каталога и скан-таблиц, которые заданы в
 * коде (правило 19). Проверяется решение программы — предупреждение или его отсутствие.
 */

#include "configFixtures.h"

#include "harmony/core/configCheck.h"
#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

const std::filesystem::path kFixture =
    std::filesystem::path(HARMONY_TEST_DATA_DIR) / "fixtures" / "xml-unknown-target.xml";

constexpr const char* kMissingKey = "the model has no key “%1”: the assignment will not work";
constexpr const char* kTemporaryName = "%1: temporary name, choose one from the list";

/// Документ фикстуры с целевой моделью @p target; пусто — свойство цели удаляется (M3).
harmony::ConfigDocument openDocument(const std::string& target) {
    auto opened = harmony::ConfigDocument::open(kFixture);
    EXPECT_TRUE(opened) << (opened ? "" : opened.error().message);
    if (!opened) {
        return harmony::ConfigDocument::createNew("fixture");
    }
    const auto result = opened->setTargetRemote(target);
    EXPECT_TRUE(result) << result.error().message;
    return std::move(*opened);
}

const harmony::Problem* problem(const std::vector<harmony::Problem>& problems,
                                const std::string& pattern) {
    const auto it = std::ranges::find_if(problems, [&](const harmony::Problem& p) {
        return p.what.format == pattern;
    });
    return it != problems.end() ? &*it : nullptr;
}

/// Проблема про имя @p key с шаблоном текста @p pattern.
const harmony::Problem* problemFor(const std::vector<harmony::Problem>& problems,
                                   const std::string& pattern, const std::string& key) {
    const auto it = std::ranges::find_if(problems, [&](const harmony::Problem& p) {
        return p.what.format == pattern && p.hardKey == key;
    });
    return it != problems.end() ? &*it : nullptr;
}

// Полный список модели (arch 14) не знает SoftRight4 → предупреждение.
TEST(CheckModelKeys, MissingKeyOnCompleteModelWarns) {
    const auto doc = openDocument("650");

    const auto problems = doc.check();
    const auto* found = problem(problems, kMissingKey);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->severity, harmony::Problem::Severity::Warning);
    EXPECT_EQ(found->hardKey, "SoftRight4");
    EXPECT_EQ(found->id, harmony::test::kTvId);   // фикстура: устройство 100
}

// Модель, которая знает клавишу (таблица 885 — код 0x2C SoftRight4), не получает предупреждения.
TEST(CheckModelKeys, KnownKeyIsNotAProblem) {
    const auto doc = openDocument("885");
    EXPECT_EQ(problem(doc.check(), kMissingKey), nullptr);
}

// Неполная таблица семейства (arch 8: 880 не измеряли) ничего не утверждает.
TEST(CheckModelKeys, IncompleteTableDoesNotAssert) {
    const auto doc = openDocument("880");
    EXPECT_EQ(problem(doc.check(), kMissingKey), nullptr);
}

// Модель без скан-таблицы (525) — список не исчерпывающий.
TEST(CheckModelKeys, ModelWithoutTableDoesNotAssert) {
    const auto doc = openDocument("525");
    EXPECT_EQ(problem(doc.check(), kMissingKey), nullptr);
}

// Документ «без модели»: свойство удалено, модель отвечает каталог по skin файла (факт M2:
// skin по умолчанию 61 → 900), и список клавиш 900 не исчерпывающий → предупреждений нет.
TEST(CheckModelKeys, DocumentWithoutModelHasNoModelCheck) {
    const auto doc = openDocument("");
    EXPECT_EQ(doc.targetRemote(), "900") << "skin файла по умолчанию 61 — открытый вопрос U2";
    EXPECT_EQ(problem(doc.check(), kMissingKey), nullptr);
}

// Ядро не видит содержимое профиля: для 1100 список не исчерпывающий, имён ячеек экрана в нём
// нет — предупреждений на них быть не может.
TEST(CheckModelKeys, ScreenCellNamesAreNotPhysicalKeys) {
    const auto doc = openDocument("1100");
    EXPECT_EQ(problem(doc.check(), kMissingKey), nullptr);
}

// Имя вне словаря (пометка скан-кода) — отдельное предупреждение про временное имя.
TEST(CheckModelKeys, NameOutsideDictionaryIsTemporary) {
    auto doc = openDocument("650");
    const auto result = doc.setHardButton(harmony::ButtonOwner::device(harmony::test::kTvId),
                                          "Scan2C", harmony::test::kTvId, "Five");
    ASSERT_TRUE(result) << result.error().message;

    const auto problems = doc.check();
    ASSERT_NE(problemFor(problems, kTemporaryName, "Scan2C"), nullptr);

    // Одно имя — одно предупреждение: про временное имя, а не дважды.
    EXPECT_EQ(problemFor(problems, kMissingKey, "Scan2C"), nullptr)
        << "Scan2C is not a key of the model, but the placeholder name is the problem to report";
}

// Предупреждение про имя вне словаря не зависит от полноты списка клавиш: оно говорит
// о самом имени, а не о модели.
TEST(CheckModelKeys, TemporaryNameWarnsEvenWithIncompleteTable) {
    auto doc = openDocument("880");   // arch 8: таблица неполная
    const auto result = doc.setHardButton(harmony::ButtonOwner::device(harmony::test::kTvId),
                                          "Scan2C", harmony::test::kTvId, "Five");
    ASSERT_TRUE(result) << result.error().message;

    const auto problems = doc.check();
    const auto* found = problem(problems, kTemporaryName);
    ASSERT_NE(found, nullptr) << "the name itself is a placeholder, completeness does not matter";
    EXPECT_EQ(found->hardKey, "Scan2C");
}

} // namespace
