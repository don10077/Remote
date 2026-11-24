/**
 * @file remoteReadOutcomeTest.cpp
 * @brief I4: решение после чтения с пульта — открывать редактор или оставить файл.

Прежнее поведение окна: прочитанное всегда оставалось файлом, решение входа не спрашивалось.
Поэтому на этапе А красными были только два новых решения — галочка включена и вход ответил
документом, значит открывать редактор. Зелёные факты: без галочки файл остаётся файлом, и то,
что таблица читателей не читает, остаётся файлом всегда.

Само окно (`MainWindow`) в тестовые цели не входит: проверяется решение, которое оно использует.
 */

#include "../app/src/remoteReadOutcome.h"

#include "configFixtures.h"
#include "formatFixtures.h"
#include "zipFixtures.h"

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <string>

using harmony::app::readOutcome;

namespace {

using harmony::ConfigDocument;
using harmony::app::ReadOutcome;
using harmony::formats::Bytes;

std::string text(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

/// Ответ входа по фикстуре, записанной в файл: вход решает, что за файл.
harmony::Result<ConfigDocument> entryAnswer(harmony::test::TempFolder& folder, const Bytes& bytes) {
    const auto path = folder.path() / "read.remote.json";
    harmony::test::writeFile(path, text(bytes));
    return ConfigDocument::open(path);
}

// Красное на этапе А: галочка включена и читатель таблицы открыл эти данные — редактор.
TEST(ReadOutcome, TickOnAndEntryAnsweredADocumentOpensTheEditor) {
    harmony::test::TempFolder folder("read-outcome-zip");
    const auto opened = entryAnswer(folder, harmony::test::zipEzHex(harmony::test::kConfigXml));
    ASSERT_TRUE(opened) << opened.error().message;
    EXPECT_EQ(readOutcome(opened, true), ReadOutcome::OpenInEditor);
}

// Красное на этапе А: то же решение для второго читателя — окно не знает о форматах.
TEST(ReadOutcome, TickOnAndTheOtherReaderOpensTheEditor) {
    harmony::test::TempFolder folder("read-outcome-image");
    const auto opened = entryAnswer(folder, harmony::test::minimalEzHex(14, 72));
    ASSERT_TRUE(opened) << opened.error().message;
    EXPECT_EQ(readOutcome(opened, true), ReadOutcome::OpenInEditor);
}

// Факт: без галочки прочитанное только сохраняется, даже если вход его открывает.
TEST(ReadOutcome, TickOffKeepsTheFileEvenWhenTheEntryOpenedIt) {
    harmony::test::TempFolder folder("read-outcome-tick-off");
    const auto opened = entryAnswer(folder, harmony::test::zipEzHex(harmony::test::kConfigXml));
    ASSERT_TRUE(opened) << opened.error().message;
    EXPECT_EQ(readOutcome(opened, false), ReadOutcome::KeepFile);
}

// Факт: данные, которые читатель таблицы не читает, остаются файлом всегда.
TEST(ReadOutcome, NoReaderKeepsTheFileEvenWithTheTickOn) {
    harmony::test::TempFolder folder("read-outcome-no-reader");

    harmony::formats::EzHex file;
    file.identity.architecture = 15;
    file.identity.skin = 61;
    file.payload = Bytes{'n', 'o', 't', ' ', 'a', ' ', 'z', 'i', 'p'};   // признака нет
    const auto bytes = harmony::formats::writeEzHex(file);
    ASSERT_TRUE(bytes) << bytes.error().message;

    const auto opened = entryAnswer(folder, *bytes);
    ASSERT_FALSE(opened) << "вход не может это открыть";
    EXPECT_EQ(readOutcome(opened, true), ReadOutcome::KeepFile);
    EXPECT_EQ(readOutcome(opened, false), ReadOutcome::KeepFile);
}

} // namespace
