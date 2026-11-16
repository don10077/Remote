/**
 * @file digest.cpp
 * @brief SHA-256 по FIPS 180-4 и Base64 по RFC 4648.
 */

#include "harmony/core/formats/digest.h"

#include <array>

namespace harmony::formats {

namespace {

constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

/// Обрабатывает один блок 64 байта.
void compress(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[4 * i]) << 24) | (static_cast<std::uint32_t>(block[4 * i + 1]) << 16) |
               (static_cast<std::uint32_t>(block[4 * i + 2]) << 8) | static_cast<std::uint32_t>(block[4 * i + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = state;
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kRound[i] + w[i];
        const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    const std::array<std::uint32_t, 8> add{a, b, c, d, e, f, g, h};
    for (std::size_t i = 0; i < 8; ++i) {
        state[i] += add[i];
    }
}

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Значение символа Base64; -1 — не из алфавита.
int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

std::string sha256Hex(std::span<const std::uint8_t> data) {
    std::array<std::uint32_t, 8> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::size_t whole = data.size() - data.size() % 64;
    for (std::size_t offset = 0; offset < whole; offset += 64) {
        compress(state, data.data() + offset);
    }
    // Хвост: остаток, 0x80, нули и длина в битах (big-endian) — один или два блока.
    std::array<std::uint8_t, 128> tail{};
    const std::size_t rest = data.size() - whole;
    for (std::size_t i = 0; i < rest; ++i) {
        tail[i] = data[whole + i];
    }
    tail[rest] = 0x80;
    const std::size_t tailSize = rest < 56 ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    for (std::size_t i = 0; i < 8; ++i) {
        tail[tailSize - 1 - i] = static_cast<std::uint8_t>(bits >> (8 * i));
    }
    for (std::size_t offset = 0; offset < tailSize; offset += 64) {
        compress(state, tail.data() + offset);
    }
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out += kHex[(word >> shift) & 0xF];
        }
    }
    return out;
}

std::string base64Encode(std::span<const std::uint8_t> data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (const std::size_t rest = data.size() - i; rest > 0) {
        std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        if (rest == 2) {
            n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        }
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += rest == 2 ? kAlphabet[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const int pad = last ? (text[i + 3] == '=' ? (text[i + 2] == '=' ? 2 : 1) : 0) : 0;
        std::uint32_t n = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            if (j >= 4 - static_cast<std::size_t>(pad)) {
                n <<= 6;
                continue;
            }
            const int value = base64Value(text[i + j]);
            if (value < 0) {
                return std::nullopt; // в том числе «=» не на своём месте
            }
            n = (n << 6) | static_cast<std::uint32_t>(value);
        }
        out.push_back(static_cast<std::uint8_t>(n >> 16));
        if (pad < 2) out.push_back(static_cast<std::uint8_t>(n >> 8));
        if (pad < 1) out.push_back(static_cast<std::uint8_t>(n));
    }
    return out;
}

} // namespace harmony::formats
