/**
 * @file digestTest.cpp
 * @brief Тесты SHA-256 и Base64: эталонные значения FIPS 180-4 / RFC 4648 и границы блоков.
 */

#include "harmony/core/formats/digest.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace harmony::formats;

namespace {

std::vector<std::uint8_t> bytesOf(const std::string& text) {
    return {text.begin(), text.end()};
}

} // namespace

TEST(Digest, Sha256MatchesReferenceValues) {
    EXPECT_EQ(sha256Hex({}), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex(bytesOf("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Digest, Sha256HandlesPaddingAcrossBlockBoundaries) {
    // 55 байт — длина ещё помещается в блок; 56 — нужен второй блок; 64 — ровно блок.
    EXPECT_EQ(sha256Hex(bytesOf(std::string(55, 'a'))),
              "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    EXPECT_EQ(sha256Hex(bytesOf(std::string(56, 'a'))),
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT_EQ(sha256Hex(bytesOf(std::string(64, 'a'))),
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST(Digest, Base64MatchesRfc4648Examples) {
    EXPECT_EQ(base64Encode({}), "");
    EXPECT_EQ(base64Encode(bytesOf("f")), "Zg==");
    EXPECT_EQ(base64Encode(bytesOf("fo")), "Zm8=");
    EXPECT_EQ(base64Encode(bytesOf("foo")), "Zm9v");
    EXPECT_EQ(base64Encode(bytesOf("foobar")), "Zm9vYmFy");
}

TEST(Digest, Base64RoundTripsAllByteValues) {
    std::vector<std::uint8_t> all(256);
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<std::uint8_t>(i);
    for (std::size_t size = 0; size <= all.size(); ++size) {
        const std::vector<std::uint8_t> part(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(size));
        const auto decoded = base64Decode(base64Encode(part));
        ASSERT_TRUE(decoded) << size;
        EXPECT_EQ(*decoded, part) << size;
    }
}

TEST(Digest, Base64RejectsMalformedText) {
    for (const char* text : {"Zm9", "Zg=a", "Z===", "Zm 9", "Zm9v\n", "Zg==Zg==", "Zm9-"}) {
        EXPECT_FALSE(base64Decode(text)) << text;
    }
}
