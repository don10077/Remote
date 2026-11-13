/**
 * @file errorTest.cpp
 * @brief Тесты ошибок по шаблону: текст, аргументы, причина.
 */

#include "harmony/core/error.h"

#include <gtest/gtest.h>

using namespace harmony;

TEST(Error, FillsPatternWithArguments) {
    const Error error(ErrorCode::NotFound, "у «%1» нет команды «%2»", {"ТВ", "Mute"});
    EXPECT_EQ(error.message, "у «ТВ» нет команды «Mute»");
    EXPECT_EQ(error.format, "у «%1» нет команды «%2»");
    EXPECT_EQ(error.args.size(), 2U);
    EXPECT_FALSE(error.cause);
}

TEST(Error, PlainTextIsItsOwnPattern) {
    const Error error(ErrorCode::Io, "пульт не подключён");
    EXPECT_EQ(error.message, error.format);
    EXPECT_TRUE(error.args.empty());
}

TEST(Error, WrapKeepsCauseAndJoinsMessages) {
    const Error cause(ErrorCode::Format, "ошибка XML в строке %1: %2", {"3", "unexpected end"});
    const auto wrapped = wrapError(cause, "%1", {"D:/a.xml"});
    EXPECT_EQ(wrapped.error().code, ErrorCode::Format);
    EXPECT_EQ(wrapped.error().message, "D:/a.xml: ошибка XML в строке 3: unexpected end");
    ASSERT_TRUE(wrapped.error().cause);
    EXPECT_EQ(wrapped.error().cause->format, "ошибка XML в строке %1: %2");

    const auto recoded = wrapError(ErrorCode::Io, cause, "чтение");
    EXPECT_EQ(recoded.error().code, ErrorCode::Io);
}

TEST(Error, MissingArgumentLeavesPlaceholder) {
    EXPECT_EQ(Error::fill("%1 и %2", {"a"}), "a и %2");
    EXPECT_EQ(Error::fill("100%", {}), "100%");
}
