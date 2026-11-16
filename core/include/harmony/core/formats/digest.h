#pragma once

/**
 * @file digest.h
 * @brief SHA-256 и Base64 без внешних библиотек — для файлов, снятых с пульта.
 *
 * Ядро не зависит от Qt и OpenSSL, а сумма и кодировка нужны и программе, и CLI.
 */

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::formats {

/**
 * @brief SHA-256 (FIPS 180-4).
 * @param data Данные.
 * @return 64 строчные шестнадцатеричные цифры.
 */
[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> data);

/**
 * @brief Base64 (RFC 4648, алфавит «+/», с дополнением «=»).
 * @param data Данные.
 * @return Текст без переводов строк.
 */
[[nodiscard]] std::string base64Encode(std::span<const std::uint8_t> data);

/**
 * @brief Разбирает Base64 строго: только алфавит RFC 4648, длина кратна 4, «=» лишь в конце.
 * @param text Текст.
 * @return Данные; nullopt — текст не Base64.
 */
[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text);

} // namespace harmony::formats
