/**
 * @file sourceImageTest.cpp
 * @brief B3: исходный образ импорта — часть документа, а не только запись архива.
 *
 * Ожидания следуют из конструкции фикстуры (правило 19): документ возвращает те же байты,
 * что получил, и после сохранения проекта. Файлы вне git тест не читает, поэтому он зелёный
 * на любой машине.
 */

#include "formatFixtures.h"

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <ranges>

namespace {

namespace test = harmony::test;

/// Документ импорта двоичного HID: фикстура GSPM, skin 72 — модель 650 каталога.
harmony::ConfigDocument importedDocument(const harmony::formats::Bytes& bytes) {
    auto document = harmony::ConfigDocument::fromEzHex(bytes, "650");
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return document ? std::move(*document) : harmony::ConfigDocument::createNew("650");
}

// Импорт сохраняет образ: документ отдаёт те же байты, что получил.
TEST(SourceImage, OriginalImageStaysWithDocument) {
    const harmony::formats::Bytes bytes = test::minimalEzHex(14, 72);
    const auto document = importedDocument(bytes);

    const auto image = document.sourceImage();
    ASSERT_TRUE(image.has_value());
    EXPECT_TRUE(std::ranges::equal(*image, bytes));
}

// «Сохранить как .EZHex» не выбрасывает образ: после повторного открытия он тот же.
TEST(SourceImage, ImageSurvivesSavingTheProject) {
    const harmony::formats::Bytes bytes = test::minimalEzHex(14, 72);
    test::TempFolder root("source-image");
    auto document = importedDocument(bytes);

    const auto file = root.path() / "project.EZHex";
    const auto saved = document.saveAsEzHex(file);
    ASSERT_TRUE(saved) << saved.error().message;

    auto reopened = harmony::ConfigDocument::open(file);
    ASSERT_TRUE(reopened) << reopened.error().message;
    const auto image = reopened->sourceImage();
    ASSERT_TRUE(image.has_value());
    EXPECT_TRUE(std::ranges::equal(*image, bytes));
}

// Документ без импорта — запасной путь: образа нет, и это не ошибка.
TEST(SourceImage, PlainProjectHasNoSourceImage) {
    const auto document = harmony::ConfigDocument::createNew("project");
    EXPECT_FALSE(document.sourceImage().has_value());
}

} // namespace
