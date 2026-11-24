/**
 * @file importTargetRemoteTest.cpp
 * @brief B4: модель документа определяется в одном месте — `targetRemote()`.
 *
 * Импорт больше не пишет свойство `HarmonyTargetRemote`: модель документа следует из skin
 * файла через каталог (M2), а свойство остаётся только выбором пользователя (M5).
 * Ожидания следуют из фикстуры (правило 19): skin 72 — модель 650 каталога.
 */

#include "formatFixtures.h"

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

namespace {

namespace test = harmony::test;

/// Фикстура двоичного импорта: skin 72 → модель 650 каталога.
const harmony::formats::Bytes kImage = test::minimalEzHex(14, 72);

// Импорт не записывает модель: её решает каталог по skin файла.
TEST(ImportTargetRemote, ImportDoesNotWriteTargetProperty) {
    const auto document = harmony::ConfigDocument::fromEzHex(kImage, "650");
    ASSERT_TRUE(document) << document.error().message;

    EXPECT_TRUE(document->model().property("HarmonyTargetRemote").empty());
    EXPECT_EQ(document->targetRemote(), "650");
}

// Решение о модели переживает сохранение проекта — его не нужно переносить свойством.
TEST(ImportTargetRemote, TargetModelSurvivesSavingTheProject) {
    test::TempFolder root("import-target");
    auto document = harmony::ConfigDocument::fromEzHex(kImage, "650");
    ASSERT_TRUE(document) << document.error().message;

    const auto file = root.path() / "project.EZHex";
    const auto saved = document->saveAsEzHex(file);
    ASSERT_TRUE(saved) << saved.error().message;

    const auto reopened = harmony::ConfigDocument::open(file);
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_TRUE(reopened->model().property("HarmonyTargetRemote").empty());
    EXPECT_EQ(reopened->targetRemote(), "650");
}

// Выбор пользователя — то же свойство, и он главнее skin (M5).
TEST(ImportTargetRemote, UserChoiceStillWinsOverSkin) {
    test::TempFolder root("import-target");
    auto document = harmony::ConfigDocument::fromEzHex(kImage, "650");
    ASSERT_TRUE(document) << document.error().message;

    ASSERT_TRUE(document->setTargetRemote("525")) << document->setTargetRemote("525").error().message;

    const auto file = root.path() / "project.EZHex";
    const auto saved = document->saveAsEzHex(file);
    ASSERT_TRUE(saved) << saved.error().message;

    const auto reopened = harmony::ConfigDocument::open(file);
    ASSERT_TRUE(reopened) << reopened.error().message;
    EXPECT_EQ(reopened->model().property("HarmonyTargetRemote"), "525");
    EXPECT_EQ(reopened->targetRemote(), "525");
}

} // namespace
